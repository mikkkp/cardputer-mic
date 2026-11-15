// tests/test_bsp_keymap.c
// TCA8418 纯逻辑层测试：FIFO 解码、7x8 → 4x14 双射、键值名覆盖、语义键。
#include "bsp_keymap.h"
#include "bsp_pins.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_decode(void) {
    bsp_key_raw_t raw;

    // bit7=1 表示按下（官方应用侧用 if (key.state) 判定按下）。
    assert(bsp_keymap_decode(0x81, &raw));   // code 1 -> (0,0)
    assert(raw.row == 0 && raw.col == 0 && raw.pressed);

    assert(bsp_keymap_decode(0x0A, &raw));   // code 10 -> (0,9)
    assert(raw.row == 0 && raw.col == 9 && !raw.pressed);

    assert(bsp_keymap_decode(0xFF & 0x7F, &raw) == false);  // code 127 越界
    assert(!bsp_keymap_decode(0x00, &raw));                 // code 0 越界
    assert(!bsp_keymap_decode(0x80, &raw));
    assert(!bsp_keymap_decode(0x81, NULL));
}

static void test_remap_is_bijective(void) {
    bool seen[BSP_KEY_LOGIC_ROWS][BSP_KEY_LOGIC_COLS] = {{false}};
    for (uint8_t r = 0; r < BSP_KEY_MATRIX_ROWS; r++) {
        for (uint8_t c = 0; c < BSP_KEY_MATRIX_COLS; c++) {
            bsp_key_raw_t raw = {r, c, true};
            bsp_key_t key;
            bsp_keymap_remap(&raw, &key);
            assert(key.row < BSP_KEY_LOGIC_ROWS);
            assert(key.col < BSP_KEY_LOGIC_COLS);
            assert(key.pressed);
            assert(!seen[key.row][key.col]);  // 56 个原始键必须映射到 56 个不同位置
            seen[key.row][key.col] = true;
        }
    }
    for (uint8_t r = 0; r < BSP_KEY_LOGIC_ROWS; r++)
        for (uint8_t c = 0; c < BSP_KEY_LOGIC_COLS; c++)
            assert(seen[r][c]);

    bsp_key_raw_t bad = {7, 0, true};
    bsp_key_t out;
    bsp_keymap_remap(&bad, &out);
    assert(out.row == 0xFF && out.col == 0xFF);
}

static void test_names_cover_all_keys(void) {
    for (uint8_t r = 0; r < BSP_KEY_LOGIC_ROWS; r++) {
        for (uint8_t c = 0; c < BSP_KEY_LOGIC_COLS; c++) {
            const char *name = bsp_keymap_name(r, c);
            assert(name != NULL && strlen(name) > 0);
        }
    }
    assert(bsp_keymap_name(4, 0) == NULL);
    assert(bsp_keymap_name(0, 14) == NULL);
    assert(strcmp(bsp_keymap_name(1, 11), "[") == 0);
    assert(strcmp(bsp_keymap_name(1, 12), "]") == 0);
    assert(strcmp(bsp_keymap_name(2, 13), "enter") == 0);
}

static void test_semantic_buttons(void) {
    // 2026-11-15 用户定版：左下角从下往上 Ctrl→听写(OK)、Fn→回车(DOWN)、Tab→撤销(UP)
    // 丝印Ctrl = 逻辑(3,0) = 本表"ctrl"位 → OK
    // 丝印Fn   = 逻辑(2,0) = 本表"shift"位 → DOWN
    // 丝印Tab  = 逻辑(1,0) = 本表"tab"位 → UP
    bsp_key_t ctrl = {3, 0, true};   // 丝印Ctrl（底）→ 听写
    bsp_key_t fn = {2, 0, true};     // 丝印Fn（中）→ 回车
    bsp_key_t tab = {1, 0, true};    // 丝印Tab（顶）→ 撤销
    bsp_key_t other = {0, 0, true};
    assert(bsp_keymap_button(&ctrl) == BSP_BTN_OK);
    assert(bsp_keymap_button(&fn) == BSP_BTN_DOWN);
    assert(bsp_keymap_button(&tab) == BSP_BTN_UP);
    // 旧映射已失效：[/]/enter 不再是语义键
    bsp_key_t old_up = {1, 11, true};
    bsp_key_t old_down = {1, 12, true};
    bsp_key_t old_ok = {2, 13, true};
    assert(bsp_keymap_button(&old_up) == BSP_BTN_COUNT);
    assert(bsp_keymap_button(&old_down) == BSP_BTN_COUNT);
    assert(bsp_keymap_button(&old_ok) == BSP_BTN_COUNT);
    assert(bsp_keymap_button(&other) == BSP_BTN_COUNT);
    assert(bsp_keymap_button(NULL) == BSP_BTN_COUNT);
}

static void test_fifo_bytes_for_semantic_keys(void) {
    // 由官方 remap 反推的三个 FIFO 字节（按下）。P1 真机按键回显时可直接比对。
    // 丝印Ctrl → 逻辑(3,0) → 原始(0,3) → code 4 → 0x84 → OK
    // 丝印Fn   → 逻辑(2,0) → 原始(0,2) → code 3 → 0x83 → DOWN
    // 丝印Tab  → 逻辑(1,0) → 原始(0,1) → code 2 → 0x82 → UP
    const struct {
        uint8_t fifo;
        bsp_btn_t button;
    } cases[] = {
        {0x84, BSP_BTN_OK},
        {0x83, BSP_BTN_DOWN},
        {0x82, BSP_BTN_UP},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        bsp_key_raw_t raw;
        bsp_key_t key;
        assert(bsp_keymap_decode(cases[i].fifo, &raw));
        assert(raw.pressed);
        bsp_keymap_remap(&raw, &key);
        assert(bsp_keymap_button(&key) == cases[i].button);
    }
}

int main(void) {
    test_decode();
    test_remap_is_bijective();
    test_names_cover_all_keys();
    test_semantic_buttons();
    test_fifo_bytes_for_semantic_keys();
    puts("test_bsp_keymap: PASS");
    return 0;
}
