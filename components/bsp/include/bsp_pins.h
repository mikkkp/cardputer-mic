// components/bsp/include/bsp_pins.h
// Cardputer ADV 硬件参数单一事实来源。引脚或 I2C 地址只在本文件定义。
//
// 证据（两个独立一手来源，2026-11-12 逐项比对吻合）：
//   A. 官方产品页 https://docs.m5stack.com/en/core/Cardputer-Adv
//      的 1/5 PinMap 表与 Product Comparison 表。
//   B. 本地官方 UserDemo 源码 ../userdemo-adv/ ：
//      main/hal/hal_config.h                     (键盘 INT、microSD SPI)
//      main/hal/keyboard/keybaord.cpp            (TCA8418 0x34、7x8 矩阵、remap)
//      components/M5Unified/src/M5Unified.cpp   (I2C/SPI 板级表、ES8311 I2S、寄存器序列)
//      components/M5Unified/src/utility/Power_Class.cpp (电池 ADC 通道与分压比)
//
// ⚠ 交接文档 §3.1 曾把 microSD 的 G12/G14/G40/G39 误标为 I2C/I2S；本表已纠正。
//
// 全部使用纯整数宏，便于 tests/test_bsp_pins.py 直接解析，也便于纯逻辑模块
// 在不引入 ESP-IDF 头文件的条件下复用。传给 IDF API 时按需 cast 为对应枚举。
#pragma once

// --- LCD：ST7789V2，240x135 横屏 -------------------------------------------
#define BSP_LCD_W             240
#define BSP_LCD_H             135
#define BSP_LCD_BL            38   // DISP_BL；同时是 RGB LED 的 PWR_EN（官方对比表：共用电源使能）
#define BSP_LCD_RST           33   // RST
#define BSP_LCD_DC            34   // RS
#define BSP_LCD_MOSI          35   // DAT
#define BSP_LCD_SCLK          36   // SCK
#define BSP_LCD_CS            37   // CS
#define BSP_LCD_SPI_HOST      2    // SPI2_HOST，初值，P1 实测后定
#define BSP_LCD_PCLK_HZ       40000000
#define BSP_LCD_SPI_MODE      0
#define BSP_LCD_INVERT_COLOR  1   // P1 真机实测（2026-11-12）：invert=1 颜色才正确，与 lgfx 默认一致

// --- 内部 I2C：ES8311 / TCA8418 / BMI270 共用 -------------------------------
#define BSP_I2C_PORT          0        // I2C_NUM_0
#define BSP_I2C_SDA           8
#define BSP_I2C_SCL           9
#define BSP_I2C_CLK_HZ        400000
#define BSP_I2C_ES8311_ADDR   0x18    // 7-bit；esp_codec_dev 的 .addr 需 <<1 = 0x30
#define BSP_I2C_TCA8418_ADDR  0x34    // 7-bit
#define BSP_I2C_BMI270_ADDR   0x68    // 7-bit；首版不初始化

// --- 键盘：TCA8418 ----------------------------------------------------------
#define BSP_KEY_INT_GPIO      11
#define BSP_KEY_MATRIX_ROWS   7
#define BSP_KEY_MATRIX_COLS   8
#define BSP_KEY_LOGIC_ROWS    4
#define BSP_KEY_LOGIC_COLS    14

// --- 电池：ADC1_CH9 = GPIO10，2:1 分压 ---------------------------------------
#define BSP_BAT_ADC_UNIT      1       // ADC_UNIT_1
#define BSP_BAT_ADC_CHANNEL   9       // IDF soc/esp32s3/include/soc/adc_channel.h: ADC1_GPIO10_CHANNEL 9
#define BSP_BAT_ADC_GPIO      10
#define BSP_BAT_ADC_RATIO_NUM 2
#define BSP_BAT_ADC_RATIO_DEN 1

// --- ES8311 I2S --------------------------------------------------------------
#define BSP_I2S_PORT          0       // I2S_NUM_0
#define BSP_I2S_MCLK          (-1)    // I2S_PIN_NO_CHANGE：本板无独立 MCLK，官方配 MCLK=BCLK
#define BSP_I2S_BCLK          41      // SCLK
#define BSP_I2S_WS            43      // LRCK
#define BSP_I2S_DOUT          42      // MCU → ES8311 DSDIN（播放，首版静音）
#define BSP_I2S_DIN           46      // ES8311 ASDOUT → MCU（录音）
#define BSP_I2S_PA_CTRL       (-1)

// --- 其余板载资源：首版不使用，登记以防误占引脚 -----------------------------
#define BSP_IR_TX_GPIO        44
#define BSP_SD_CS_GPIO        12
#define BSP_SD_MOSI_GPIO      14
#define BSP_SD_CLK_GPIO       40
#define BSP_SD_MISO_GPIO      39
#define BSP_GROVE_SDA_GPIO    2
#define BSP_GROVE_SCL_GPIO    1
#define BSP_EXT_UART_TX_GPIO  13
#define BSP_EXT_UART_RX_GPIO  15
