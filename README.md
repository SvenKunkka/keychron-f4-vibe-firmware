# Keychron F4 Vibe firmware bring-up

Public bring-up firmware for the Keychron F4 Vibe prototype using the fitted
Realtek RTL8762GKU MCU.

## Implemented behavior

- Physical key 1 sends **Left Ctrl** while held.
- Pressing key 1 enables the analog MEMS microphone capture path.
- Releasing key 1 stops capture and makes USB microphone packets silent.
- The four addressable LEDs are green while key 1 is held and off otherwise.
- Physical keys 2-4 are disabled in this build.
- Wired USB only; Keychron Launcher support is disabled.

## Firmware files

The `release/` directory contains:

- `F4_Vibe_Ctrl_PTT_Green_v1.0.3.0.offer.bin`
- `F4_Vibe_Ctrl_PTT_Green_v1.0.3.0.payload.bin`
- `F4_Vibe_Ctrl_PTT_Green_v1.0.3.0_MP.bin`

Use the offer and payload pair with the RTL8762G CFU updater. The MP image is
provided for the corresponding Realtek image workflow.

SHA-256:

| File | SHA-256 |
| --- | --- |
| offer | `4cc8ec6d2d600376314a4fc66ee26b0dac84f6a5333fb36272d2cc81c14eef04` |
| payload | `235285e4e8b818817830584fc05aae64af2a7fc0e1c67d10ffd67dbc39d07a98` |
| MP image | `ba3eb4ba75a4649e126811f0318bf10159c6deaeb890663d65607f86798dc9f5` |

## Source layout

- `firmware/f4_ptt_audio.c`: microphone gate, USB Audio frames and LED state.
- `firmware/f4_wired_stubs.c`: wired-only adapters for Keychron's RTL8762G
  ZMK fork.
- `firmware/boards/shields/keychron_f4_vibe/`: F4 key map, USB configuration
  and device-tree overlay.

This repository intentionally does not redistribute the Realtek SDK or the
complete Keychron ZMK fork. Build the sources inside the matching vendor SDK
and Keychron ZMK environment.

## Validation status

The image links successfully for RTL8762GKU and contains both the USB Audio
class and F4 push-to-talk listener. At publication time, the target was not
visible to macOS in DFU mode, so flashing and assembled-unit microphone/LED
validation are still pending. Treat this as engineering bring-up firmware,
not a production release.
