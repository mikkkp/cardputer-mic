// components/bsp/include/bsp_keyboard.h
// TCA8418 键盘的硬件层：I2C 初始化序列、G11 中断、FIFO 读取。
// 纯逻辑（FIFO 字节解析 / 7x8 → 4x14 remap / 语义键）在 bsp_keymap.c，可 host test。
#pragma once

#include "bsp_keymap.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 按下/抬起事件回调。raw 为 7x8 原始坐标，key 为 4x14 逻辑坐标，
// btn 为语义键（BSP_BTN_COUNT 表示非语义键）。回调在 bsp_keyboard_poll() 的调用上下文执行。
typedef void (*bsp_keyboard_event_cb_t)(const bsp_key_raw_t *raw,
                                        const bsp_key_t *key,
                                        bsp_btn_t btn);

// 初始化：bsp_i2c_init() → 写 TCA8418 初始化序列 → CFG 读改写使能中断 →
// 配 G11 为任意边沿中断并安装 ISR。幂等，重复调用返回 ESP_OK。
esp_err_t bsp_keyboard_init(void);

// 注册事件回调；传 NULL 取消。
void bsp_keyboard_set_event_cb(bsp_keyboard_event_cb_t cb);

// 轮询并处理挂起的按键事件，返回本次处理的事件数。
// 无中断时立即返回 0。读空 FIFO 后清 INT_STAT 的 K_INT 位。
int bsp_keyboard_poll(void);

// 直接写/读 TCA8418 寄存器（调试用，初始化后可用）。
esp_err_t bsp_keyboard_reg_write(uint8_t reg, uint8_t value);
esp_err_t bsp_keyboard_reg_read(uint8_t reg, uint8_t *value);

#ifdef __cplusplus
}
#endif
