/* F4 Vibe push-to-talk microphone for the RTL8762GKU Keychron ZMK port. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/net/buf.h>
#include <zephyr/usb/class/usb_audio.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#include "rtl_codec.h"
#include "rtl_gpio.h"
#include "rtl_i2s.h"
#include "rtl_pinmux.h"
#include "rtl_rcc.h"

#define F4_PTT_POSITION 0U
#define F4_USB_RATE_HZ 48000U
#define F4_CODEC_RATE_HZ 16000U
#define F4_USB_FRAME_BYTES ((F4_USB_RATE_HZ / 1000U) * sizeof(int16_t))
#define F4_CODEC_SAMPLES_PER_USB_FRAME (F4_CODEC_RATE_HZ / 1000U)
#define F4_LED_DATA_PIN P4_2
#define F4_LED_COUNT 4U
#define F4_LED_GREEN 0x20U
#define F4_DWT_CTRL (*(volatile uint32_t *)0xE0001000UL)
#define F4_DWT_CYCCNT (*(volatile uint32_t *)0xE0001004UL)
#define F4_DEMCR (*(volatile uint32_t *)0xE000EDFCUL)

extern uint32_t SystemCoreClock;

static const struct device *const f4_mic = DEVICE_DT_GET_ONE(usb_audio_mic);
static volatile bool f4_ptt_active;
static bool f4_capture_running;
static int16_t f4_last_sample;

NET_BUF_POOL_FIXED_DEFINE(f4_audio_pool, 4, F4_USB_FRAME_BYTES, 4, NULL);

static bool f4_led_timing_ready;

static void f4_led_wait_cycles(uint32_t start, uint32_t cycles)
{
    while ((uint32_t)(F4_DWT_CYCCNT - start) < cycles) {
        __asm__ volatile("nop");
    }
}

/* LD1..LD4 are a cascaded 800 kHz GRB chain driven from MCU pin 18 / P4_2. */
static void f4_led_set(bool recording)
{
    if (!f4_led_timing_ready || SystemCoreClock < 1000000U) {
        return;
    }

    const uint32_t high_0 = (uint32_t)(((uint64_t)SystemCoreClock * 350U) / 1000000000U);
    const uint32_t high_1 = (uint32_t)(((uint64_t)SystemCoreClock * 700U) / 1000000000U);
    const uint32_t period = (uint32_t)(((uint64_t)SystemCoreClock * 1250U) / 1000000000U);
    const uint32_t pin = GPIO_GetPin(F4_LED_DATA_PIN);
    GPIO_TypeDef *const port = GPIO_GetPort(F4_LED_DATA_PIN);
    const uint8_t green = recording ? F4_LED_GREEN : 0U;
    unsigned int key = irq_lock();

    for (uint8_t led = 0; led < F4_LED_COUNT; ++led) {
        const uint8_t grb[3] = {green, 0U, 0U};
        for (uint8_t byte = 0; byte < 3U; ++byte) {
            for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U) {
                uint32_t start = F4_DWT_CYCCNT;
                port->GPIO_DR |= pin;
                f4_led_wait_cycles(start, (grb[byte] & mask) ? high_1 : high_0);
                port->GPIO_DR &= ~pin;
                f4_led_wait_cycles(start, period);
            }
        }
    }
    irq_unlock(key);
    k_busy_wait(80);
}

static void f4_led_init(void)
{
    RCC_PeriphClockCmd(APBPeriph_GPIOB, APBPeriph_GPIOB_CLOCK, ENABLE);
    Pad_Config(F4_LED_DATA_PIN, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE,
               PAD_OUT_ENABLE, PAD_OUT_LOW);
    Pinmux_Config(F4_LED_DATA_PIN, DWGPIO);
    GPIO_InitTypeDef gpio;
    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_GetPin(F4_LED_DATA_PIN);
    gpio.GPIO_Mode = GPIO_MODE_OUT;
    gpio.GPIO_ITCmd = DISABLE;
    GPIO_Init(GPIO_GetPort(F4_LED_DATA_PIN), &gpio);
    GPIO_ResetBits(GPIO_GetPort(F4_LED_DATA_PIN), GPIO_GetPin(F4_LED_DATA_PIN));

    F4_DEMCR |= (1UL << 24);
    F4_DWT_CYCCNT = 0U;
    F4_DWT_CTRL |= 1UL;
    uint32_t before = F4_DWT_CYCCNT;
    for (volatile uint8_t i = 0; i < 16U; ++i) {
        __asm__ volatile("nop");
    }
    f4_led_timing_ready = F4_DWT_CYCCNT != before;
    f4_led_set(false);
}

/*
 * The PCB routes MIC1 through C37 to P2_7/MIC_P and MICBIAS to MIC1 VDD.
 * P2_6 is not routed, so this must remain single-ended AMIC mode.
 */
static void f4_codec_init(void)
{
    Pad_Config(P2_6, PAD_SW_MODE, PAD_NOT_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(P2_7, PAD_SW_MODE, PAD_NOT_PWRON, PAD_PULL_NONE, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pad_Config(P4_0, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
    Pad_Config(P4_1, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
    Pad_Config(P4_3, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_LOW);
    Pinmux_Config(P4_0, BCLK_SPORT0);
    Pinmux_Config(P4_1, LRC_RX_SPORT0);
    Pinmux_Config(P4_3, SDO_CODEC_SLAVE);

    CODEC_AnalogCircuitInit();
    RCC_PeriphClockCmd(APBPeriph_CODEC | APBPeriph_I2S0,
                       APBPeriph_CODEC_CLOCK | APBPeriph_I2S0_CLOCK, ENABLE);

    CODEC_InitTypeDef codec;
    CODEC_StructInit(&codec);
    codec.CODEC_Ch0MicType = CODEC_CH_AMIC;
    codec.CODEC_Ch0Mute = CODEC_UNMUTE;
    codec.CODEC_Ch1Mute = CODEC_MUTE;
    codec.CODEC_MicBIAS = MICBIAS_VOLTAGE_2_025;
    codec.CODEC_MicBstMode = MICBST_Mode_Single;
    codec.CODEC_MicBstGain = MICBST_Gain_30dB;
    codec.CODEC_SampleRate0 = SAMPLE_RATE_16KHz;
    codec.CODEC_SampleRate1 = SAMPLE_RATE_16KHz;
    codec.CODEC_I2SFormat = CODEC_I2S_DataFormat_I2S;
    codec.CODEC_I2SChannelLen = I2S_CHANNELLEN_32;
    codec.CODEC_I2SRxDataWidth = CODEC_I2S_Rx_DataWidth_16Bits;
    codec.CODEC_I2SChSequence = CODEC_I2S_CH_L_R;
    CODEC_Init(CODEC, &codec);

    I2S_InitTypeDef i2s;
    I2S_StructInit(&i2s);
    i2s.I2S_ClockSource = I2S_CLK_40M;
    i2s.I2S_RxBClockMi = 0x271;
    i2s.I2S_RxBClockNi = 0x10;
    i2s.I2S_DeviceMode = I2S_DeviceMode_Master;
    i2s.I2S_RxChannelType = I2S_Channel_Mono;
    i2s.I2S_RxDataFormat = I2S_Mode;
    i2s.I2S_RxChannelWidth = I2S_Width_32Bits;
    i2s.I2S_RxDataWidth = I2S_Width_16Bits;
    I2S_Init(I2S0, &i2s);
    I2S_Cmd(I2S0, I2S_MODE_RX, DISABLE);
}

static void f4_capture_set(bool enable)
{
    if (enable == f4_capture_running) {
        return;
    }

    if (enable) {
        while (I2S_GetRxFIFOLen(I2S0) != 0U) {
            (void)I2S_ReceiveFIFOData(I2S0);
        }
        I2S_Cmd(I2S0, I2S_MODE_RX, ENABLE);
    } else {
        I2S_Cmd(I2S0, I2S_MODE_RX, DISABLE);
        f4_last_sample = 0;
    }
    f4_capture_running = enable;
}

static void f4_fill_audio_frame(uint8_t *dst, size_t len)
{
    memset(dst, 0, len);
    if (!f4_ptt_active || !f4_capture_running || len < F4_USB_FRAME_BYTES) {
        return;
    }

    int16_t codec_samples[F4_CODEC_SAMPLES_PER_USB_FRAME];
    size_t have = 0;
    uint8_t available = I2S_GetRxFIFOLen(I2S0);
    while (available-- != 0U && have < ARRAY_SIZE(codec_samples)) {
        codec_samples[have++] = (int16_t)(I2S_ReceiveFIFOData(I2S0) & 0xffffU);
    }

    int16_t *pcm = (int16_t *)dst;
    for (size_t i = 0; i < F4_CODEC_SAMPLES_PER_USB_FRAME; ++i) {
        int16_t sample = i < have ? codec_samples[i] : f4_last_sample;
        pcm[i * 3U] = sample;
        pcm[i * 3U + 1U] = sample;
        pcm[i * 3U + 2U] = sample;
        f4_last_sample = sample;
    }
}

static void f4_audio_requested(const struct device *dev)
{
    struct net_buf *buf = net_buf_alloc(&f4_audio_pool, K_NO_WAIT);
    if (buf == NULL) {
        return;
    }

    size_t frame_size = usb_audio_get_in_frame_size(dev);
    if (frame_size != F4_USB_FRAME_BYTES || net_buf_tailroom(buf) < frame_size) {
        net_buf_unref(buf);
        return;
    }

    uint8_t *data = net_buf_add(buf, frame_size);
    f4_fill_audio_frame(data, frame_size);
    if (usb_audio_send(dev, buf, frame_size) != 0) {
        net_buf_unref(buf);
    }
}

static const struct usb_audio_ops f4_audio_ops = {
    .data_request_cb = f4_audio_requested,
};

static int f4_ptt_event_listener(const zmk_event_t *event)
{
    const struct zmk_position_state_changed *position =
        as_zmk_position_state_changed(event);
    if (position == NULL || position->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL ||
        position->position != F4_PTT_POSITION) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    f4_ptt_active = position->state;
    f4_capture_set(position->state);
    f4_led_set(position->state);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(f4_ptt, f4_ptt_event_listener);
ZMK_SUBSCRIPTION(f4_ptt, zmk_position_state_changed);

static int f4_ptt_audio_init(void)
{
    if (!device_is_ready(f4_mic)) {
        return -ENODEV;
    }

    f4_led_init();
    f4_codec_init();
    f4_capture_set(false);
    usb_audio_register(f4_mic, &f4_audio_ops);
    return 0;
}

/* Register the microphone function before ZMK calls usb_enable(). */
SYS_INIT(f4_ptt_audio_init, APPLICATION, 40);
