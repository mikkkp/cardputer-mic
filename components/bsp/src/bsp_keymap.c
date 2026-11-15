// components/bsp/src/bsp_keymap.c
#include "bsp_keymap.h"
#include "bsp_pins.h"
#include <stddef.h>

// 官方 UserDemo main/hal/keyboard/keybaord.cpp 的 _key_value_map[4][14] 第一列名。
// 注意：这块 56 键键盘没有独立 Esc 键，也没有独立方向键；首版只用三个语义键，
// 与上游 mic_app.c 的 bsp_btn_t(UP/DOWN/OK) 完全对齐，避免引入第四个枚举。
static const char *const k_names[BSP_KEY_LOGIC_ROWS][BSP_KEY_LOGIC_COLS] = {
    {"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "del"},
    {"tab", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\\"},
    {"shift", "capslock", "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "enter"},
    {"ctrl", "opt", "alt", "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", " "},
};

bool bsp_keymap_decode(uint8_t fifo, bsp_key_raw_t *out) {
    if (!out) return false;
    // KEY_EVENT_A: bit7 = 1 表示按下（官方应用侧普遍用 if (key.state) 判定按下）。
    // 低 7 位是 1..80 的键码；先减 1，再 row = code/10、col = code%10。
    uint8_t code = fifo & 0x7F;
    if (code == 0 || code > 80) return false;
    code--;
    out->row = (uint8_t)(code / 10);
    out->col = (uint8_t)(code % 10);
    out->pressed = (fifo & 0x80) != 0;
    return true;
}

void bsp_keymap_remap(const bsp_key_raw_t *raw, bsp_key_t *out) {
    if (!raw || !out) return;
    out->pressed = raw->pressed;
    if (raw->row >= BSP_KEY_MATRIX_ROWS || raw->col >= BSP_KEY_MATRIX_COLS) {
        out->row = 0xFF;
        out->col = 0xFF;
        return;
    }
    // 官方 remap()：col = row*2 + (col>3)，row = (col+4)%4。
    out->col = (uint8_t)(raw->row * 2 + (raw->col > 3 ? 1 : 0));
    out->row = (uint8_t)((raw->col + 4) % 4);
}

const char *bsp_keymap_name(uint8_t row, uint8_t col) {
    if (row >= BSP_KEY_LOGIC_ROWS || col >= BSP_KEY_LOGIC_COLS) return NULL;
    return k_names[row][col];
}

bsp_btn_t bsp_keymap_button(const bsp_key_t *key) {
    if (!key || !bsp_keymap_name(key->row, key->col)) return BSP_BTN_COUNT;
    // 语义键映射（2026-11-15 用户定版）：
    //   左下角三个丝印键，从下往上：Ctrl（底）→ Fn（中）→ Tab（顶）
    //   物理位置对应本表：Ctrl=row3col0（"ctrl"位）、Fn=row2col0（"shift"位）、
    //   Tab=row1col0（"tab"位）。
    //   功能（从下往上）：听写、回车、撤销。
    if (key->row == 3 && key->col == 0) return BSP_BTN_OK;   // Ctrl（底）→ 听写
    if (key->row == 2 && key->col == 0) return BSP_BTN_DOWN; // Fn（中）→ 回车
    if (key->row == 1 && key->col == 0) return BSP_BTN_UP;   // Tab（顶）→ 撤销
    return BSP_BTN_COUNT;
}
