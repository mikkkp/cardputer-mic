# Cardputer Mic

[简体中文](README.zh_CN.md) · **English**

BLE voice-microphone firmware for the M5Stack Cardputer ADV, ported from the
verified [Passport Mic](https://github.com/MatchlessHeroHx/ai-passport-mic)
project (MIT). The BLE protocol and the Mac companion are unchanged:
`BLE → BlackHole 2ch → Doubao IME`.

**Status: production-ready (P0–P4 complete).** All core features have been
validated on-device. See [NOTICE](NOTICE) for upstream attribution.

## What it does

Press the **Ctrl** key (bottom-left) to dictate into any text field on your Mac
via Bluetooth LE. The firmware streams 16 kHz / 16-bit mono IMA-ADPCM audio to
the Mac companion, which feeds it to Doubao IME for speech-to-text.

| Key (silkscreen) | Function |
|---|---|
| **Ctrl** (bottom-left) | Start / stop dictation |
| **Fn** (middle) | Confirm / Enter |
| **Tab** (top) | Undo last transcription |

Battery percentage is estimated by coulomb counting (NVS-persisted,
±10–15% accuracy) and shown in the top-right corner. Charge for 1.5 h via
USB-C to auto-calibrate to 100%.

## Hardware facts

Stamp-S3A / ESP32-S3FN8, 8 MB Flash, **no PSRAM**, 240×135 ST7789V2,
56-key TCA8418, ES8311 + NS4150B. The battery SAR ADC on GPIO10 is
**unusable** on this board (analog domain shared with BT PHY / USB-JTAG;
readings corrupt within milliseconds of init). Battery level is estimated
by software coulomb counting instead.

The authoritative pin table lives in
[`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h)
and is checked by `tests/test_bsp_pins.py`.

Key values: internal I2C SDA/SCL `G8/G9`; ES8311 I2S `SCLK=G41`,
`ASDOUT→MCU=G46`, `LRCK=G43`, `DSDIN←MCU=G42`, **no independent MCLK**
(the official driver configures `MCLK=BCLK`); TCA8418 INT `G11`.
`G38` is `DISP_BL` and also the shared `PWR_EN` for the RGB LED.

## Build

```bash
source ~/esp/esp-idf/export.sh
./tools/validate.sh --static      # host tests, no toolchain needed
./tools/validate.sh --firmware    # ESP-IDF 5.5.3 build + merged 0x0 image
```

## Flashing

```bash
source ~/esp/esp-idf/export.sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/cardputer-mic-full.bin
```

A pre-built merged image is in [`firmware/`](firmware/).

## Validation

```bash
./tools/validate.sh               # full gate (static + firmware)
```

On-device acceptance completed:
- BLE pairing, connection, passkey flow
- 10×30 s continuous dictation (0 sequence gaps, 0 CRC errors)
- 3× disconnect/reconnect recovery (worst-case 1.84 s to READY)
- Three-level mic distance test (1/5/15 cm, no clipping)
- End-to-end dictation into Doubao IME via companion menu-bar app
- Coulomb-counting battery display (NVS persistence, USB charge calibration)

## Project layout

```
components/bsp/    Board Support Package (pins, I2C, audio, display, keyboard, keymap)
main/              Application (state machine, UI, BLE, protocol, battery model)
tests/             Host tests (keymap, protocol, UI render, battery SOC)
tools/             Validation, font generation, BLE probes
```

## Known limitations

- **Battery percentage accuracy is ±10–15%** (coulomb-counting estimate; the
  SAR ADC hardware is unusable for voltage measurement on this board).
- No LE Audio / native Bluetooth microphone profile (ESP32-S3 does not
  support it in ESP-IDF 5.5.3); requires the Mac companion app.
- USB-C is charge-only + flashing (no USB Audio Class).
- `G43` (I2S LRCK) conflicts with UART0 TX, so `CONFIG_ESP_CONSOLE_NONE`
  is mandatory — serial logs are disabled by design.
