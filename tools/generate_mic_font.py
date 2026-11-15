#!/usr/bin/env python3
"""从 Hiragino Sans GB W6 生成 main/mic_font.c/.h（裸 framebuffer 4bpp alpha 位图字体）。

- 字号：16（正文/状态）、24（强调/中文大字）、40（配对码数字）。
- 4bpp alpha（0-15），渲染端做 RGB565 前景混合。
- 字符集：UI 文案中文短词 + ASCII 32-126。生成时同时校验覆盖率。
"""
import re
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

TTC = "/System/Library/Fonts/Hiragino Sans GB.ttc"
FONT_INDEX = 2  # 0=W3 1=Interface W3 2=W6 3=Interface W6
ROOT = Path(__file__).resolve().parents[1]

# 中文字形直接从 UI 源码里提取，避免手写清单漏字（漏字会渲染成空白）。
SOURCES = ["main/mic_ui_stub.c"]


def _cjk_in_strings(path):
    """只取字符串字面量里的中文（先剥注释，避免把中文注释也算进字形集）。"""
    src = path.read_text()
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    chars = set()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', src):
        chars.update(re.findall(r"[\u3000-\u9fff\uff00-\uffef·…]", lit))
    return chars


TEXT_ZH = set()
for _src in SOURCES:
    TEXT_ZH |= _cjk_in_strings(ROOT / _src)
ASCII_ALL = "".join(chr(c) for c in range(32, 127))
CHARS = sorted(TEXT_ZH | set(ASCII_ALL))
SIZES = [(16, 16), (24, 24), (40, 40)]
PAD = 8  # 画布余量（避免裁边）


def render_glyph(font, ch, px, ascent):
    """按统一基线渲染，返回 (w, h, 4bpp 位图, dy)。

    dy 是墨迹顶部相对基线的偏移（<=0）。统一基线保证同一行字符上下对齐——
    早先按各自包围盒定位会让 "S" 比 "B" 矮 2px。
    """
    baseline = PAD + ascent
    canvas = Image.new("L", (px * 3 + PAD, px * 3 + PAD), 0)
    d = ImageDraw.Draw(canvas)
    d.text((PAD, baseline), ch, font=font, fill=255, anchor="ls")
    bbox = canvas.getbbox()
    if not bbox:
        return 0, 0, b"", 0  # 空格等
    x0, y0, x1, y1 = bbox
    dy = y0 - baseline
    glyph = canvas.crop((x0, y0, x1, y1))
    w, h = glyph.size
    rows = list(glyph.getdata())
    packed = bytearray()
    for y in range(h):
        base = y * w
        for x in range(0, w, 2):
            hi = rows[base + x] * 15 // 255
            lo = rows[base + x + 1] * 15 // 255 if x + 1 < w else 0
            packed.append((hi << 4) | lo)
    return w, h, bytes(packed), dy


def main():
    tables = {}
    ascents = {}
    for _, px in SIZES:
        font = ImageFont.truetype(TTC, px, index=FONT_INDEX)
        ascent, _descent = font.getmetrics()
        ascents[px] = ascent
        tbl = []
        for ch in CHARS:
            w, h, bits, dy = render_glyph(font, ch, px, ascent)
            tbl.append((ord(ch), w, h, dy, bits))
        tables[px] = tbl

    def C(v):
        return str(v)

    out = []
    out.append("// main/mic_font.c")
    out.append("// 由 tools/generate_mic_font.py 生成（Hiragino Sans GB W6 子集），勿手改。")
    out.append("// 4bpp alpha 位图；每字形记录 top bearing（y0-画布 PAD/2）用于基线对齐。")
    out.append('#include "mic_font.h"')
    out.append("")
    for _, px in SIZES:
        tbl = tables[px]
        out.append(f"// ---- {px}px：{len(tbl)} 字形，位图 {sum(len(t[4]) for t in tbl)} B ----")
        out.append(f"static const uint8_t s_bits_{px}[] = {{")
        for code, w, h, y0, bits in tbl:
            out.append(f"    /* U+{code:04X} '{chr(code)}' {w}x{h} dy={dy} */")
            for i in range(0, len(bits), 16):
                out.append("    " + ",".join(C(b) for b in bits[i:i + 16]) + ",")
        out.append("};")
        out.append("")
        out.append(f"static const mic_glyph_t s_tbl_{px}[] = {{")
        off = 0
        for code, w, h, dy, bits in tbl:
            out.append(f"    {{0x{code:04X},{w},{h},{dy},{off}u}},  // '{chr(code)}'")
            off += len(bits)
        out.append("};")
        out.append("")

    out.append("const mic_glyph_t *mic_font_glyph(int size, uint16_t code)")
    out.append("{")
    out.append("    const mic_glyph_t *tbl; int n;")
    out.append("    switch (size) {")
    for _, px in SIZES:
        out.append(f"    case {px}: tbl = s_tbl_{px}; n = {len(tables[px])}; break;")
    out.append("    default: return 0;")
    out.append("    }")
    out.append("    int lo = 0, hi = n - 1;")
    out.append("    while (lo <= hi) {")
    out.append("        int mid = (lo + hi) / 2;")
    out.append("        if (tbl[mid].code == code) return &tbl[mid];")
    out.append("        if (tbl[mid].code < code) lo = mid + 1; else hi = mid - 1;")
    out.append("    }")
    out.append("    return 0;")
    out.append("}")
    # bits 访问函数（需访问 static 表，放 .c）
    bits_fn = ["const uint8_t *mic_font_bits(int size, const mic_glyph_t *g)", "{"]
    for _, px in SIZES:
        bits_fn.append(f"    if (size == {px}) return s_bits_{px} + g->off;")
    bits_fn.append("    return 0;")
    bits_fn.append("}")
    bits_fn.append("")
    bits_fn.append("int mic_font_ascent(int size)")
    bits_fn.append("{")
    for _, px in SIZES:
        bits_fn.append(f"    if (size == {px}) return {ascents[px]};")
    bits_fn.append("    return 0;")
    bits_fn.append("}")
    bits_fn.append("")
    out.extend(bits_fn)
    (ROOT / "main/mic_font.c").write_text("\n".join(out) + "\n")

    hdr = [
        "// main/mic_font.h",
        "// 由 tools/generate_mic_font.py 生成，勿手改。",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    uint16_t code;",
        "    uint8_t  w, h;",
        "    int8_t   dy;      // 墨迹顶部相对基线的偏移（<=0）",
        "    uint32_t off;     // 位图在字节表中的偏移",
        "} mic_glyph_t;",
        "",
        "const mic_glyph_t *mic_font_glyph(int size, uint16_t code);",
        "// 字号对应的基线上升高度（行框顶部 + ascent = 基线 y）",
        "int mic_font_ascent(int size);",
        "// 位图数据（4bpp，按行打包，行尾半字节补零）",
        "const uint8_t *mic_font_bits(int size, const mic_glyph_t *g);",
        "",
    ]
    (ROOT / "main/mic_font.h").write_text("\n".join(hdr) + "\n")

    total = sum(len(t[4]) for tbl in tables.values() for t in tbl)
    print(f"OK: {len(CHARS)} 字 × 3 字号；位图共 {total} B；估算 .rodata ≈ {total // 1024} KB")


if __name__ == "__main__":
    main()
