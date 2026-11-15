#!/usr/bin/env python3
"""UI 渲染回归：校验主机侧预览输出不是空屏/错位。

检查项（全部来自实际渲染的 PPM，而不是源码意图）：
1. 尺寸必须正好 240x135，字节数 240*135*3 + 头部。
2. 画面不能是纯色（有文字/图标）。
3. 背景必须是深色（防字节序回退成白底）。
4. 配对码视图必须在主区出现强调色像素（大号数字确实画出来了）。
5. 录音视图必须有波形柱，且**小电平**也能撑起柱高（防止显示增益回退）；
   静音视图不能出现高柱（防止噪声门失效）。
6. 电量未知视图（battery=-2）右上角必须与 0% 视图不同，且有 "--%" 的浅色文字；
   防止把失真的 ADC 读数画成误导性的 0%，或退化成 USB 插头图标。
"""
import sys
from pathlib import Path


def load(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"P6"):
        raise AssertionError(f"{path}: 不是 P6 PPM")
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    assert w == 240 and h == 135, f"{path}: 尺寸 {w}x{h} 应为 240x135"
    assert len(px) == w * h * 3, f"{path}: 像素数据 {len(px)} 字节不符"
    return w, h, px


def get(px, w, x, y):
    i = (y * w + x) * 3
    return px[i], px[i + 1], px[i + 2]


def colors(px, w, h):
    seen = set()
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            seen.add(get(px, w, x, y))
    return seen


def check_ready(path):
    w, h, px = load(path)
    uniq = colors(px, w, h)
    assert len(uniq) > 8, f"{path}: 只有 {len(uniq)} 种颜色，疑似空屏"
    bg = get(px, w, 120, 30)          # 主区背景
    assert max(bg) < 150 and sum(bg) < 330, f"{path}: 背景 {bg} 过亮（字节序回退？）"
    return len(uniq)


def check_pair(path):
    w, h, px = load(path)
    accent = 0                          # 配对码用高饱和强调色：亮度高且色差明显
    for y in range(52, 108):
        for x in range(20, 220):
            r, g, b = get(px, w, x, y)
            if max(r, g, b) > 170 and max(r, g, b) - min(r, g, b) > 60:
                accent += 1
    assert accent > 400, f"{path}: 配对码区域强调色像素仅 {accent}，数字没画出来"
    return accent


def bar_columns(px, w, y0, y1):
    """数有多少根波形柱在 y0..y1 范围内出现了高饱和亮色像素。"""
    n = 0
    for x in range(58, 236, 6):
        for y in range(y0, y1):
            r, g, b = get(px, w, x + 2, y)
            if max(r, g, b) > 140 and max(r, g, b) - min(r, g, b) > 50:
                n += 1
                break
    return n


def check_record(path, tall_min=12, tall_max=31):
    w, h, px = load(path)
    cols = bar_columns(px, w, 84, 108)          # 柱体存在
    assert 15 <= cols <= tall_max, f"{path}: 波形柱 {cols} 列，异常"
    tall = bar_columns(px, w, 70, 84)           # 上半段：小声音也要顶上来
    assert tall >= tall_min, f"{path}: 只有 {tall} 根柱子超过 24px，显示增益回退了？"
    assert tall <= tall_max, f"{path}: 高柱 {tall} 根，疑似把噪声放大满屏"
    return cols


def check_quiet(path):
    w, h, px = load(path)
    tall = bar_columns(px, w, 70, 84)
    assert tall <= 2, f"{path}: 静音仍有 {tall} 根高柱，噪声门失效"
    return tall


def check_unknown(path, zero_path):
    """右上角电量区（x 175..233, y 3..20）：未知态必须显示 "--%"。"""
    w, h, px = load(path)
    _, _, zpx = load(zero_path)
    # 与 0% 视图逐像素对比：0% 用 CORAL(255,122,138) 数字 + 空电池壳，
    # 未知态用 DIM(182,196,232) 的 "--%"，两处必须有实质差异。
    diff = 0
    dim = 0
    for y in range(3, 21):
        for x in range(175, 234):
            r, g, b = get(px, w, x, y)
            zr, zg, zb = get(zpx, w, x, y)
            if (r, g, b) != (zr, zg, zb):
                diff += 1
            # COL_DIM 系浅蓝白文字（前景混合后 r/g/b 都偏高、蓝略高于红）
            if r > 120 and g > 140 and b > 170 and b >= r:
                dim += 1
    assert diff > 60, f"{path}: 电量未知态与 0% 视图仅 {diff} 像素不同，疑似仍显示 0%"
    assert dim > 25, f"{path}: 右上角浅色文字像素仅 {dim}，未知态文字没画出来或退化成插头图标"
    return diff, dim


def main():
    d = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    n1 = check_ready(d / "ui.ppm")
    n2 = check_pair(d / "ui_pair.ppm")
    n3 = check_record(d / "ui_rec.ppm")
    n4 = check_quiet(d / "ui_quiet.ppm")
    n5, n6 = check_unknown(d / "ui_unknown.ppm", d / "ui_zero.ppm")
    print(f"test_ui_render: PASS（配色 {n1} 色 / 配对码 {n2} 像素 / 波形 {n3} 列 / 静音高柱 {n4} / 电量未知差异 {n5}·文字 {n6}）")


if __name__ == "__main__":
    main()
