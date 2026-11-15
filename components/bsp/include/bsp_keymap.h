// components/bsp/include/bsp_keymap.h
// TCA8418 键盘事件的纯逻辑层：FIFO 字节解析、7x8 → 4x14 重映射、语义键映射。
// 不依赖 ESP-IDF / I2C，可由 host tests 直接编译。
//
// 逐行对照官方 UserDemo 的 main/hal/keyboard/keybaord.cpp 与 keymap.h：
//   get_key_event_raw(): bit7=1 表示按下（应用侧普遍用 if (key.state) 判定按下）；
//                       低 7 位先减 1，再 row = code/10, col = code%10。
//   remap(): col = row*2 + (col>3), row = (col+4)%4。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 与上游 mic_app.c 的 bsp_btn_t 语义保持一致，P2 可零改动接入。
typedef enum {
    BSP_BTN_UP = 0,
    BSP_BTN_DOWN,
    BSP_BTN_OK,
    BSP_BTN_COUNT,   // 非语义键时返回此值
} bsp_btn_t;

typedef struct {
    uint8_t row;      // 7x8 原始矩阵行
    uint8_t col;      // 7x8 原始矩阵列
    bool    pressed;  // true = 按下
} bsp_key_raw_t;

typedef struct {
    uint8_t row;      // 4x14 逻辑矩阵行
    uint8_t col;      // 4x14 逻辑矩阵列
    bool    pressed;
} bsp_key_t;

// 解析 KEY_EVENT_A 的一个 FIFO 字节。code 越界（0 或 >80）返回 false。
bool bsp_keymap_decode(uint8_t fifo, bsp_key_raw_t *out);

// 官方 remap。输入坐标越界时输出置为 0xFF 并保持 pressed，便于调用方过滤。
void bsp_keymap_remap(const bsp_key_raw_t *raw, bsp_key_t *out);

// 4x14 逻辑坐标 → 语义键。非语义键返回 BSP_BTN_COUNT。
bsp_btn_t bsp_keymap_button(const bsp_key_t *key);

// 4x14 逻辑坐标 → 官方键值表的 ASCII 名（仅用于日志与 host test）。
// 越界返回 NULL。
const char *bsp_keymap_name(uint8_t row, uint8_t col);
