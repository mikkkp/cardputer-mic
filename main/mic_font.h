// main/mic_font.h
// 由 tools/generate_mic_font.py 生成，勿手改。
#pragma once
#include <stdint.h>

typedef struct {
    uint16_t code;
    uint8_t  w, h;
    int8_t   dy;      // 墨迹顶部相对基线的偏移（<=0）
    uint32_t off;     // 位图在字节表中的偏移
} mic_glyph_t;

const mic_glyph_t *mic_font_glyph(int size, uint16_t code);
// 字号对应的基线上升高度（行框顶部 + ascent = 基线 y）
int mic_font_ascent(int size);
// 位图数据（4bpp，按行打包，行尾半字节补零）
const uint8_t *mic_font_bits(int size, const mic_glyph_t *g);

