// components/bsp/include/bsp_display.h
// ST7789V2 240x135 横屏的最小 bring-up 驱动（esp_lcd，无 LVGL）。
// P1 只负责：点亮、上电序列、画四角标记以标定 offset / MADCTL / 反色。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 SPI 总线、esp_lcd 面板与 G38 背光。幂等。
esp_err_t bsp_display_init(void);

// 画一张诊断画面：四角色块 + 中心十字 + 底部颜色条。
// 四角分别为 左上红 / 右上绿 / 左下蓝 / 右下白；若方向或颜色不符，
// 说明 offset / swap_xy / mirror / invert 需要按实物再调。
void bsp_display_draw_test(void);

// 背光开关（G38 高有效）。
void bsp_display_backlight(bool on);

// 面板息屏/亮屏（DISPOFF，不含背光）。
esp_err_t bsp_display_set_on(bool on);

// 全屏填充一帧（RGB565 little-endian）。
esp_err_t bsp_display_fill(const uint16_t *frame);

// 屏幕逻辑宽高（= BSP_LCD_W/H），供 UI 计算布局。
#define BSP_UI_W 240
#define BSP_UI_H 135

#ifdef __cplusplus
}
#endif
