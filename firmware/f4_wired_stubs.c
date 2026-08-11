/* Wired-only adapters for Keychron's RTL8762G ZMK fork. */
#include <stdbool.h>
#include <stdint.h>

#include <drivers/behavior.h>
#include <zmk/endpoints_types.h>

/* RTL8762G ROM data symbol required by the SDK codec power helper. */
__asm__(".global pmu_auto_switch_map_force_base_level\n"
        ".set pmu_auto_switch_map_force_base_level, 0x0013d944\n");

bool bat_enable_dlps = true;
bool settings_enabled_dlps = true;

enum zmk_transport get_hardware_select_transport(void) { return ZMK_TRANSPORT_USB; }

int zmk_ble_init(void) { return 0; }
int zmk_ble_prof_disconnect(uint8_t index) { return 0; }
int zmk_ble_active_profile_index(void) { return 0; }
void zmk_ble_reconn(void) {}
void zmk_ble_sleep(void) {}

int zmk_ppt_send_keyboard_report(uint8_t *report, uint8_t len) { return -1; }
int zmk_ppt_send_consumer_report(uint8_t *report, uint8_t len) { return -1; }
int zmk_ppt_send_mouse_report(uint8_t *report, uint8_t len) { return -1; }
void zmk_ppt_reconn(void) {}
void zmk_ppt_disconnect(void) {}
bool zmk_ppt_is_ready(void) { return false; }
uint8_t zmk_ppt_get_state(void) { return 0; }

static bool force_sleep;
void set_force_sleep(bool enable) { force_sleep = enable; }
bool force_to_sleep(void) { return force_sleep; }

bool bat_is_shutdown(void) { return false; }
void bat_low_check(uint8_t keypress) {}
void stop_battery_timer(void) {}

void leds_pwroff(void) {}
void blue_led_set_state(uint8_t state, uint8_t index) {}
void led_24G_set_state(uint8_t state) {}
void led_recover(uint8_t stop_rgb) {}
int ble_is_pairing(void) { return -1; }

void launcher_update_nkro(uint8_t nkro) {}
bool dip_switch_update_user(uint8_t index, bool active) { return true; }

uint8_t get_report_rate(void) { return 1; }
uint32_t get_default_scan_period(void) { return 1000; }
void user_set_scan_period(uint32_t period) {}

struct zmk_behavior_binding *get_zmk_encode_map(uint8_t layer) { return NULL; }
