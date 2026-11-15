# Tools

- `validate.sh` — full CI gate (static + firmware). No external dependencies.
- `generate_mic_font.py` — regenerate the CJK bitmap font from `mic_ui_stub.c` string literals. Requires Pillow + macOS system fonts.
- `preview_ui.sh` — render UI framebuffer to PNG for local preview. Requires cc (clang/gcc).
- `verify_firmware.py` — verify merged firmware image layout and checksum.
- `ble_probe.py` / `p3_observe.py` / `p4_probe.py` — BLE test probes for on-device validation. Require a Mac with `bleak` (Python BLE library) and the companion app running.

> BLE probes are development tools; end users don't need them for normal use.
