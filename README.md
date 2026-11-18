# Cardputer Mic

[简体中文](README.zh_CN.md) · **English**

BLE voice-microphone firmware for the M5Stack Cardputer ADV, ported from the
[Passport Mic](https://github.com/MatchlessHeroHx/ai-passport-mic) project (MIT).
The BLE protocol and the Mac companion are unchanged:
`BLE → BlackHole 2ch → Doubao IME`.

## What it does

Press the **Ctrl** key (bottom-left) to dictate into any text field on your Mac
via Bluetooth LE. The firmware streams 16 kHz / 16-bit mono IMA-ADPCM audio to
the Mac companion, which feeds it to Doubao IME for speech-to-text.

| Key (silkscreen) | Function |
|---|---|
| **Ctrl** (bottom) | Start / stop dictation |
| **Fn** (middle) | Confirm / Enter |
| **Tab** (top) | Undo last transcription |

Battery percentage is a software estimate shown in the top-right corner.
Active use drains it; leaving the device idle with the screen off for 15 minutes
is treated as charging, and about 2 hours of accumulated idle time calibrates it
to 100%. No voltage sensing is involved.

## Hardware

Stamp-S3A / ESP32-S3FN8, 8 MB Flash, **no PSRAM**, 240×135 ST7789V2, 56-key
TCA8418, ES8311 + NS4150B, 1500 mAh battery. The battery SAR ADC on GPIO10 is
**unusable** on this board (analog domain shared with the BT PHY and USB-JTAG;
readings degrade within milliseconds of init). The battery level is therefore a
software estimate (NVS-persisted mAh drain/restore model), not a measurement.

The authoritative pin table lives in
[`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h) and is
checked by `tests/test_bsp_pins.py`.

Key values: internal I2C SDA/SCL `G8/G9`; ES8311 I2S `SCLK=G41`,
`ASDOUT→MCU=G46`, `LRCK=G43`, `DSDIN←MCU=G42`, **no independent MCLK**
(the official driver configures `MCLK=BCLK`); TCA8418 INT `G11`.
`G38` is `DISP_BL` and also the shared `PWR_EN` for the RGB LED.

## Build

Requires ESP-IDF v5.5.3.

```bash
source ~/esp/esp-idf/export.sh
./tools/validate.sh --static      # host tests, no toolchain needed
./tools/validate.sh --firmware    # build + merged 0x0 image
./tools/validate.sh               # full gate (static + firmware)
```

## Flashing

```bash
source ~/esp/esp-idf/export.sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/cardputer-mic-full.bin
```

A pre-built merged image is in [`firmware/`](firmware/); the latest is
`cardputer-mic-v1.1.0.bin`.

## Project layout

```text
components/bsp/    Board support: pins, I2C, audio, display, keyboard, keymap
main/              Application: state machine, UI, BLE, protocol, battery model
tests/             Host tests: keymap, protocol, UI render, ADPCM, battery SOC
tools/             Validation gate, font generation, UI preview, BLE probes
firmware/          Pre-built merged images (v1.0.0, v1.1.0)
```

## Known limitations

- **Battery percentage is a rough estimate, not a measurement** — the SAR ADC
  hardware is unusable for voltage sensing on this board. Charging is inferred
  from idle time rather than a USB signal, so leaving the device idle without a
  charger can still raise the displayed value.
- USB connection is not detected reliably: `usb_serial_jtag_is_connected()`
  monitors host SOF packets, which wall chargers never send and macOS may stop
  when the port is suspended.
- No LE Audio or native Bluetooth microphone profile (not supported by the
  ESP32-S3 in ESP-IDF 5.5.3); the Mac companion app is required.
- USB-C is charge-only plus flashing (no USB Audio Class).
- `G43` (I2S LRCK) conflicts with UART0 TX, so `CONFIG_ESP_CONSOLE_NONE` is
  mandatory — serial logs are disabled by design.

## Credits

Upstream firmware and protocol by
[MatchlessHeroHx/ai-passport-mic](https://github.com/MatchlessHeroHx/ai-passport-mic)
and [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), both MIT.
See [NOTICE](NOTICE).
