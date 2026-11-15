# P1：共享 I2C 总线 + 纯逻辑模块 + 键盘（TCA8418）+ 屏幕（ST7789V2）。
# P1b：音频（ES8311 裸寄存器 + 原生 I2S，不依赖 esp_codec_dev）。
idf_component_register(
    SRCS "src/bsp_i2c.c"
         "src/bsp_keymap.c"
         "src/bsp_tca8418.c"
         "src/bsp_battery_soc.c"
         "src/bsp_keyboard.c"
         "src/bsp_display.c"
         "src/bsp_audio.c"
         "src/bsp_battery.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_driver_gpio esp_driver_spi esp_lcd esp_driver_i2s esp_adc
)
