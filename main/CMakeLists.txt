# P2：mic 核心（协议/BLE/ADPCM/省电时钟）+ 占位 UI + 键盘语义入口。
idf_component_register(
    SRCS "mic_main.c"
         "mic_battery_model.c"
         "mic_protocol.c"
         "mic_adpcm.c"
         "mic_ble.c"
         "mic_screen.c"
         "mic_ui_stub.c"
         "mic_font.c"
    INCLUDE_DIRS "."
    REQUIRES bsp driver bt nvs_flash
)
