// main/mic_ui_stub.c
// Cardputer Mic 正式 UI：240x135 裸 framebuffer（非 LVGL），保持 mic_ui_stub.h 接口签名。
//
// 设计要点
//   · 统一基线排版：字符按 mic_font 的 ascent + 相对基线偏移落位，行内不再各自错位。
//   · 背景一次算好并做 Bayer 抖动：565 只有 5/6/5 级，深色渐变必然出色带，
//     抖动把台阶换成细密网点，肉眼看才是连续过渡。
//   · 吉祥物「小麦」：圆形话筒机器人，按状态换表情（就绪眨眼 / 录音张嘴 / 离线睡觉 / 配对好奇）。
//   · 动画全部由 now 驱动（主循环 20fps 调用 update），不额外建任务、不加定时器。
//   · 帧缓冲按 ST7789 要求以大端字节序存放，见 be16()。
#include "mic_ui_stub.h"
#include "bsp_display.h"
#include "mic_font.h"
#include "esp_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mic_ui";

#define FB_PIXELS (BSP_UI_W * BSP_UI_H)
static uint16_t s_fb[FB_PIXELS];
static uint16_t s_bg[FB_PIXELS];   // 抖动后的背景，逐帧 memcpy

// ---- 调色板 ---------------------------------------------------------------
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COL_BG_TOP   RGB(0x27, 0x37, 0x7A)  // 渐变顶（比上一版更亮，减少死气）
#define COL_BG_BOT   RGB(0x11, 0x16, 0x35)  // 渐变底
#define COL_BAR      RGB(0x2B, 0x38, 0x78)  // 状态栏底
#define COL_BAR_BOT  RGB(0x1A, 0x21, 0x4C)  // 底栏底
#define COL_FG       RGB(0xFF, 0xFF, 0xFF)
#define COL_DIM      RGB(0xB6, 0xC4, 0xE8)
#define COL_MINT     RGB(0x4A, 0xE3, 0xB0)
#define COL_CYAN     RGB(0x4C, 0xC9, 0xF5)
#define COL_AMBER    RGB(0xFF, 0xC4, 0x3D)
#define COL_CORAL    RGB(0xFF, 0x7A, 0x8A)
#define COL_PINK     RGB(0xF7, 0x82, 0xC8)
#define COL_PURPLE   RGB(0xB4, 0x9B, 0xFF)
#define COL_INK      RGB(0x0E, 0x14, 0x30)  // 深色文字（画在亮色块上）

// ---- 像素原语 --------------------------------------------------------------
/* ST7789 走 SPI 大端序，ESP32 是小端：帧缓冲按大端存放，读回时再换回来。 */
static uint16_t be16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

static void px(int x, int y, uint16_t c)
{
    if (x < 0 || x >= BSP_UI_W || y < 0 || y >= BSP_UI_H) return;
    s_fb[y * BSP_UI_W + x] = be16(c);
}

static uint16_t at(int x, int y)
{
    if (x < 0 || x >= BSP_UI_W || y < 0 || y >= BSP_UI_H) return COL_BG_BOT;
    return be16(s_fb[y * BSP_UI_W + x]);
}

static uint16_t mix(uint16_t a, uint16_t b, int t)  // t: 0..15
{
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return (uint16_t)(((ar * (15 - t) + br * t) / 15) << 11) |
           (uint16_t)(((ag * (15 - t) + bg * t) / 15) << 5) |
           (uint16_t)((ab * (15 - t) + bb * t) / 15);
}

static void fill(int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) px(i, j, c);
}

static void fill_round(int x, int y, int w, int h, int r, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        int inset = 0;
        if (j < r) inset = r - j - 1;
        else if (j >= h - r) inset = j - (h - r);
        if (inset < 0) inset = 0;
        if (inset > w / 2) inset = w / 2;
        for (int i = inset; i < w - inset; i++) px(x + i, y + j, c);
    }
}

static void disc(int cx, int cy, int r, uint16_t c)
{
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            if (i * i + j * j <= r * r) px(cx + i, cy + j, c);
}

static void ring(int cx, int cy, int r, int thick, uint16_t c)
{
    int ri = r - thick;
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++) {
            int d = i * i + j * j;
            if (d <= r * r && d >= ri * ri) px(cx + i, cy + j, c);
        }
}

static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int ax = x1 > x0 ? x1 - x0 : x0 - x1;
    int ay = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    int err = ax - ay;
    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -ay) { err -= ay; x0 += sx; }
        if (e2 < ax) { err += ax; y0 += sy; }
    }
}

// ---- 背景：渐变 + 柔光 + Bayer 抖动 ---------------------------------------
// 4x4 Bayer 阈值矩阵（0..15），用来在相邻两个色阶之间按位置取舍。
static const uint8_t kBayer[4][4] = {
    { 0, 8, 2, 10}, {12, 4, 14, 6}, { 3, 11, 1, 9}, {15, 7, 13, 5}
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static uint16_t get_bg_at(int x, int y) { return be16(s_bg[y * BSP_UI_W + x]); }

static void build_bg(void)
{
    const int W = BSP_UI_W, H = BSP_UI_H;
    for (int y = 0; y < H; y++) {
        float t = (float)y / (float)(H - 1);
        float br = 0x27 + (0x11 - 0x27) * t;
        float bg = 0x37 + (0x16 - 0x37) * t;
        float bb = 0x7A + (0x35 - 0x7A) * t;
        for (int x = 0; x < W; x++) {
            // 顶部偏左的柔光，给深色底一点层次（一次算完，不影响帧率）
            float gx = (x - 96.0f) / 190.0f, gy = (y + 46.0f) / 170.0f;
            float gd = sqrtf(gx * gx + gy * gy);
            float glow = gd < 1.0f ? (1.0f - gd) * (1.0f - gd) : 0.0f;
            // 四角轻微压暗，避免整块平
            float vx = (x - W * 0.5f) / (W * 0.5f), vy = (y - H * 0.45f) / (H * 0.5f);
            float vd = sqrtf(vx * vx + vy * vy);
            float vig = vd > 0.82f ? (vd - 0.82f) * 26.0f : 0.0f;

            float r = br + glow * 24.0f - vig;
            float g = bg + glow * 30.0f - vig;
            float b = bb + glow * 52.0f - vig * 1.4f;

            // 抖动：±半格阈值，抖散 565 量化台阶
            float dith = ((float)kBayer[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f;
            int r5 = clampi((int)(r * 31.0f / 255.0f + dith + 0.5f), 0, 31);
            int g6 = clampi((int)(g * 63.0f / 255.0f + dith + 0.5f), 0, 63);
            int b5 = clampi((int)(b * 31.0f / 255.0f + dith + 0.5f), 0, 31);
            s_bg[y * W + x] = be16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
        }
    }
}

// ---- 文字（按统一基线排版）-------------------------------------------------
static int char_w(uint32_t cp, int size)
{
    const mic_glyph_t *g = mic_font_glyph(size, (uint16_t)cp);
    return (g ? g->w : size / 2) + (size >= 24 ? 2 : 1);
}

static int text_width(const char *s, int size)
{
    int x = 0;
    while (*s) {
        uint32_t cp = (uint8_t)*s++;
        if (cp & 0x80) {
            if ((cp & 0xE0) == 0xC0 && *s) {
                cp = ((cp & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
            } else if ((cp & 0xF0) == 0xE0 && s[0] && s[1]) {
                cp = ((cp & 0x0F) << 12) | (((uint8_t)s[0] & 0x3F) << 6) | ((uint8_t)s[1] & 0x3F);
                s += 2;
            } else {
                continue;
            }
        }
        x += char_w(cp, size);
    }
    return x;
}

/* y_top 是行框顶部；内部换算成基线，再按字形的 dy（相对基线）落笔，
   这样 "USB" 的 S 不会比 B 高一截、中英混排也坐在同一条基线上。 */
static int draw_text(const char *s, int x, int y_top, uint16_t c, int size)
{
    const int baseline = y_top + mic_font_ascent(size);
    while (*s) {
        uint32_t cp = (uint8_t)*s++;
        if (cp & 0x80) {
            if ((cp & 0xE0) == 0xC0 && *s) {
                cp = ((cp & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
            } else if ((cp & 0xF0) == 0xE0 && s[0] && s[1]) {
                cp = ((cp & 0x0F) << 12) | (((uint8_t)s[0] & 0x3F) << 6) | ((uint8_t)s[1] & 0x3F);
                s += 2;
            } else {
                continue;
            }
        }
        const mic_glyph_t *g = mic_font_glyph(size, (uint16_t)cp);
        if (!g) { x += size / 2; continue; }
        const uint8_t *bits = mic_font_bits(size, g);
        const int stride = (g->w + 1) / 2;
        for (int gy = 0; gy < g->h; gy++) {
            int dy = baseline + g->dy + gy;
            if (dy < 0 || dy >= BSP_UI_H) continue;
            for (int gx = 0; gx < g->w; gx++) {
                uint8_t byte = bits[gy * stride + gx / 2];
                int a = (gx & 1) ? (byte & 0x0F) : (byte >> 4);
                if (a) px(x + gx, dy, mix(at(x + gx, dy), c, a));
            }
        }
        x += g->w + (size >= 24 ? 2 : 1);
    }
    return x;
}

static void text_center(const char *s, int y_top, uint16_t c, int size)
{
    draw_text(s, (BSP_UI_W - text_width(s, size)) / 2, y_top, c, size);
}

// ---- 图标 ------------------------------------------------------------------
static void icon_ble(int x, int y, uint16_t c)   // 14x14
{
    line(x + 4, y + 1, x + 4, y + 13, c);
    line(x + 4, y + 1, x + 9, y + 4, c);
    line(x + 9, y + 4, x + 1, y + 11, c);
    line(x + 4, y + 13, x + 9, y + 10, c);
    line(x + 9, y + 10, x + 1, y + 3, c);
}

/* 电量：外壳 20x12 + 右侧正极。外形高度接近数字的字高，
   描边用接近白色的亮度，避免"细灰框 + 粗白字"的重量不匹配。 */
static void icon_battery(int x, int y, int pct, uint16_t shell, uint16_t bgc)
{
    fill(x, y, 22, 12, bgc);                        // 清底，避免残影
    for (int i = 0; i < 20; i++) { px(x + i, y, shell); px(x + i, y + 11, shell); }
    for (int j = 1; j < 11; j++) { px(x, y + j, shell); px(x + 19, y + j, shell); }
    fill(x + 20, y + 3, 2, 6, shell);               // 正极
    int cells = (clampi(pct, 0, 100) * 16 + 50) / 100;
    uint16_t fc = pct <= 20 ? COL_CORAL : (pct <= 50 ? COL_AMBER : COL_MINT);
    if (cells > 0) fill(x + 2, y + 2, cells, 8, fc);
}

static void icon_plug(int x, int y, uint16_t c)    // 22x12，与电池同框
{
    fill(x, y, 22, 12, COL_BAR);
    fill(x + 5, y + 3, 9, 7, COL_BAR);
    for (int i = 0; i < 9; i++) { px(x + 5 + i, y + 3, c); px(x + 5 + i, y + 9, c); }
    for (int j = 1; j < 7; j++) { px(x + 5, y + 3 + j, c); px(x + 13, y + 3 + j, c); }
    fill(x + 14, y + 4, 3, 5, c);
    line(x + 2, y + 5, x + 4, y + 5, c);
}

// ---- 吉祥物「小麦」：圆形话筒机器人 ----------------------------------------
static void draw_mascot(int cx, int cy, mic_state_t st, uint32_t now, int level)
{
    uint16_t body = COL_CYAN;
    if (st == MIC_RECORDING) body = COL_CORAL;
    else if (st == MIC_READY) body = COL_MINT;
    else if (st == MIC_OFFLINE) body = COL_PURPLE;

    const int r = 15;
    if (st == MIC_RECORDING || st == MIC_STARTING) {
        int extra = 2 + level * 6 / 15;
        ring(cx, cy, r + 3 + extra, 1, mix(COL_CORAL, COL_AMBER, (int)((now / 90) % 15)));
    }

    // 触角：两颗小圆点，轻微上下摆
    int bob = (int)((now / 300) % 2);
    disc(cx - 8, cy - r - 1 - bob, 2, mix(body, COL_INK, 4));
    disc(cx + 8, cy - r - 1 - (1 - bob), 2, mix(body, COL_INK, 4));

    disc(cx, cy, r, mix(body, COL_INK, 3));
    disc(cx, cy, r - 2, body);

    if (st == MIC_READY || st == MIC_RECORDING) {
        disc(cx - 10, cy + 3, 2, mix(body, COL_PINK, 9));
        disc(cx + 10, cy + 3, 2, mix(body, COL_PINK, 9));
    }

    const int ey = cy - 4;
    const uint16_t ink = COL_INK;
    if (st == MIC_OFFLINE) {
        for (int i = -3; i <= 3; i++) {
            px(cx - 7 + i, ey + 2 - (i * i) / 6, ink);
            px(cx + 7 + i, ey + 2 - (i * i) / 6, ink);
        }
        for (int k = 0; k < 3; k++) {
            int ph = (int)((now / 40 + k * 26) % 78);
            int zy = cy - 20 - ph / 3;
            int zx = cx + 10 + k * 6 + ph / 14;
            int a = 15 - ph * 15 / 78;
            if (a > 1 && zy > 2 && zy < BSP_UI_H - 22) {
                uint16_t zc = mix(at(zx, zy), COL_DIM, a);
                line(zx, zy, zx + 4, zy, zc);
                line(zx + 4, zy, zx, zy + 4, zc);
                line(zx, zy + 4, zx + 4, zy + 4, zc);
            }
        }
    } else if (st == MIC_RECORDING) {
        if (((now / 170) % 4) == 0) {
            fill(cx - 9, ey, 6, 2, ink);
            fill(cx + 4, ey, 6, 2, ink);
        } else {
            disc(cx - 6, ey, 3, ink);
            disc(cx + 6, ey, 3, ink);
            px(cx - 7, ey - 2, COL_FG);
            px(cx + 5, ey - 2, COL_FG);
        }
        int mouth = 2 + level * 5 / 15;
        disc(cx, cy + 6, mouth, ink);
        disc(cx, cy + 6 + mouth / 2, mouth / 2 + 1, COL_PINK);
    } else if (st == MIC_STARTING) {
        static const int dxs[12] = {0, 1, 2, 3, 3, 3, 2, 1, 0, -1, -2, -3};
        static const int dys[12] = {-3, -3, -2, -1, 0, 1, 2, 3, 3, 3, 2, 1};
        int a = (int)((now / 120) % 12);
        disc(cx - 6 + dxs[a], ey + dys[a] / 2, 2, ink);
        disc(cx + 6 + dxs[a], ey + dys[a] / 2, 2, ink);
        fill(cx - 3, cy + 5, 6, 2, ink);
    } else {
        bool blink = (now % 2600) > 2400;
        if (blink) {
            fill(cx - 9, ey + 1, 6, 2, ink);
            fill(cx + 4, ey + 1, 6, 2, ink);
        } else {
            disc(cx - 6, ey, 3, ink);
            disc(cx + 6, ey, 3, ink);
            px(cx - 7, ey - 2, COL_FG);
            px(cx + 5, ey - 2, COL_FG);
        }
        for (int i = -4; i <= 4; i++) px(cx + i, cy + 8 - (i * i) / 6, ink);
    }

    line(cx, cy + r + 1, cx, cy + r + 3, mix(COL_DIM, COL_INK, 6));
    line(cx - 5, cy + r + 4, cx + 5, cy + r + 4, mix(COL_DIM, COL_INK, 6));
}

// ---- 状态 ---------------------------------------------------------------
static int  s_pairing = -1;
// 显示增益：设备端正常说话的 peak 只有几百到几千，而旧代码按 30000 满量程映射，
// 结果每根柱子只有 1~3px。这里做自适应归一化 + 平方根压缩，让小声也能铺满柱高。
#define VIS_CEIL_MIN  900
#define VIS_CEIL_MAX  20000
#define VIS_NOISE     180
static int  s_vis_ceil = 1600;
static int  s_level_hold;        // 0..240 定点，含峰值保持
static int  s_level;             // 0..15 当前显示电平
static bool s_audio_ok = true;
static int  s_battery = -1;
static bool s_battery_valid;
static bool s_battery_unknown;
static const mic_model_t *s_model;
static uint32_t s_last_render;
static uint32_t s_rec_start;
static uint32_t s_last_action;
static uint8_t  s_last_action_kind;
static int      s_wave[30];    // 存 0..15 的显示电平，不再存原始 peak
static int      s_wave_pos;

/* 每 50ms 喂一次原始 peak（int16 绝对值），产出 0..15 的显示电平。
   上限快速跟随、缓慢回落；平方根压缩让强弱层次在小屏上拉开；
   低于噪声门直接归零，静音时波形保持 1px 底线而不是满屏乱跳。 */
static void feed_level(int peak)
{
    if (peak < 0) peak = 0;
    if (peak > s_vis_ceil) s_vis_ceil += (peak - s_vis_ceil) / 2 + 1;
    else s_vis_ceil -= (s_vis_ceil - VIS_CEIL_MIN) / 24 + 1;
    if (s_vis_ceil < VIS_CEIL_MIN) s_vis_ceil = VIS_CEIL_MIN;
    if (s_vis_ceil > VIS_CEIL_MAX) s_vis_ceil = VIS_CEIL_MAX;

    int lv = 0;
    if (peak > VIS_NOISE) {
        float ratio = (float)peak / (float)s_vis_ceil;
        if (ratio > 1.0f) ratio = 1.0f;
        lv = (int)(sqrtf(ratio) * 15.0f + 0.5f);
        if (lv < 1) lv = 1;
        if (lv > 15) lv = 15;
    }
    int target = lv * 16;
    if (target > s_level_hold) s_level_hold = target;
    else s_level_hold = s_level_hold * 7 / 8;
    s_level = s_level_hold / 16;
}

// 状态栏/底栏几何（统一在这里算，避免各行各自微调导致视觉不齐）
#define BAR_H      24
#define TEXT_TOP   3                     // 16px 字在 24px 栏里的行框顶部
#define ICON_Y     7                     // 电池/插头图标顶部，与数字视觉中线对齐
#define BOTTOM_H   20
#define BOTTOM_TOP (BSP_UI_H - BOTTOM_H)
#define BOTTOM_TXT (BOTTOM_TOP + 1)      // 底栏 16px 字行框顶部

static void render(uint32_t now)
{
    if (now - s_last_render < 50) return;   // 20fps
    s_last_render = now;

    memcpy(s_fb, s_bg, sizeof(s_bg));       // 背景已抖动好，直接搬

    mic_state_t st = s_model ? s_model->state : MIC_OFFLINE;

    // ===== 顶部状态栏 =====
    fill(0, 0, BSP_UI_W, BAR_H, COL_BAR);
    fill(0, BAR_H - 2, BSP_UI_W, 2, s_pairing >= 0 ? COL_PURPLE
                                  : (st == MIC_RECORDING ? COL_CORAL
                                  : (st == MIC_READY ? COL_MINT : COL_BAR)));

    // 左上：连接状态胶囊
    const char *st_txt;
    uint16_t chip;
    if (s_pairing >= 0)           { st_txt = "配对中"; chip = COL_PURPLE; }
    else if (st == MIC_RECORDING) { st_txt = "录音中"; chip = COL_CORAL; }
    else if (st == MIC_STARTING)  { st_txt = "启动中"; chip = COL_AMBER; }
    else if (st == MIC_READY)     { st_txt = "已连接"; chip = COL_MINT; }
    else                          { st_txt = "未连接"; chip = COL_PURPLE; }
    int tw = text_width(st_txt, 16);
    fill_round(6, TEXT_TOP, tw + 21, 18, 6, mix(chip, COL_INK, 8));
    icon_ble(9, TEXT_TOP + 2, chip);
    draw_text(st_txt, 24, TEXT_TOP, chip, 16);

    // 中间：录音计时
    if (st == MIC_RECORDING && s_rec_start) {
        uint32_t sec = (now - s_rec_start) / 1000;
        char t[12];
        snprintf(t, sizeof(t), "%02u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
        int w = text_width(t, 16);
        int tx = 118 - w / 2;
        disc(tx - 6, TEXT_TOP + 9, 2, ((now / 500) % 2) ? COL_CORAL : mix(COL_CORAL, COL_BAR, 11));
        draw_text(t, tx, TEXT_TOP, COL_FG, 16);
    }

    // 右上：有效电量 / 电量未知 / USB 供电图标，三态互不混淆
    if (s_battery_valid) {
        int pct = clampi(s_battery, 0, 100);
        char b[16];
        snprintf(b, sizeof(b), "%d%%", pct);
        int w = text_width(b, 16);
        int tx = BSP_UI_W - 7 - w;
        draw_text(b, tx, TEXT_TOP, pct <= 20 ? COL_CORAL : COL_FG, 16);
        icon_battery(tx - 4 - 22, ICON_Y, pct, mix(COL_FG, COL_BAR, 3), COL_BAR);
    } else if (s_battery_unknown) {
        /* 纯电池但 SAR ADC 读数被 BT PHY 压低：不显示误导性的 0%，
           用 "--%" + 空电池壳表示「有电池，但本固件读不准」。 */
        const char *t = "--%";
        int w = text_width(t, 16);
        int tx = BSP_UI_W - 7 - w;
        draw_text(t, tx, TEXT_TOP, COL_DIM, 16);
        icon_battery(tx - 4 - 22, ICON_Y, 0, mix(COL_FG, COL_BAR, 3), COL_BAR);
    } else {
        icon_plug(BSP_UI_W - 7 - 22, ICON_Y, mix(COL_FG, COL_BAR, 3));
    }

    // ===== 主区 =====
    if (s_pairing >= 0) {
        text_center("请输入配对码", 32, COL_FG, 16);
        char code[10];
        snprintf(code, sizeof(code), "%06u", (unsigned)s_pairing);
        text_center(code, 52, COL_PINK, 40);
    } else if (st == MIC_RECORDING || st == MIC_STARTING) {
        draw_mascot(34, 66, st, now, s_level);
        const char *big = st == MIC_RECORDING ? "正在录音" : "请稍候";
        draw_text(big, 62, 30, st == MIC_RECORDING ? COL_CORAL : COL_AMBER, 24);
        if (!s_audio_ok) {
            draw_text("麦克风异常", 62, 58, COL_CORAL, 16);
        } else {
            // 30 柱历史波形，柱高 38px（旧版只有 26px 且按 30000 满量程，几乎不动）
            for (int i = 0; i < 30; i++) {
                int lv = s_wave[(s_wave_pos + i) % 30];
                if (lv > 15) lv = 15;
                int h = 1 + lv * 37 / 15;
                int bx = 58 + i * 6;
                uint16_t col = lv >= 13 ? COL_CORAL : (lv >= 9 ? COL_AMBER
                             : (lv >= 5 ? COL_CYAN : COL_MINT));
                for (int j = 0; j < h; j++) {
                    uint16_t c = (j >= h - 2) ? mix(col, COL_FG, 9)
                               : (j < 2 ? mix(col, COL_INK, 5) : col);
                    fill(bx, 108 - j, 4, 1, c);
                }
            }
            // 15 段 LED 电平表：亮几格直接对应当前音量，比单纯的波形柱更好读
            for (int k = 0; k < 15; k++) {
                int sx = 58 + k * 12;
                fill(sx, 110, 10, 3, mix(get_bg_at(sx, 110), COL_INK, 3));
                if (k < s_level) {
                    uint16_t lc = k >= 12 ? COL_CORAL : (k >= 9 ? COL_AMBER : COL_MINT);
                    fill(sx, 110, 9, 3, lc);
                }
            }
        }
    } else {
        draw_mascot(42, 70, st, now, 0);
        bool ready = st == MIC_READY;
        draw_text(ready ? "话筒就绪" : "等待连接", 76, 42,
                  ready ? COL_MINT : COL_PURPLE, 24);
        draw_text(ready ? "轻按Ctrl说话" : "蓝牙未连接", 76, 74, COL_DIM, 16);
    }

    // ===== 底部提示条 =====
    fill(0, BOTTOM_TOP, BSP_UI_W, BOTTOM_H, COL_BAR_BOT);
    fill(0, BOTTOM_TOP, BSP_UI_W, 1, mix(COL_BAR_BOT, COL_FG, 2));
    if (s_pairing >= 0) {
        text_center("在 Mac 上确认配对码", BOTTOM_TXT, COL_PURPLE, 16);
    } else if (st == MIC_OFFLINE) {
        text_center("请打开 Mac 端伴测程序", BOTTOM_TXT, COL_DIM, 16);
    } else {
        uint16_t k1 = (s_last_action_kind == 2 && now - s_last_action < 1500) ? COL_CYAN : COL_DIM;
        uint16_t k2 = (s_last_action_kind == 1 && now - s_last_action < 1500) ? COL_CYAN : COL_DIM;
        /* 底栏三段对称：总宽~168px，240px 屏四等分间隔 18px。
           k1 对应 Fn回车（kind 2 = CONFIRM），k2 对应 Tab撤销（kind 1 = UNDO）。 */
        draw_text("Ctrl听写", 18, BOTTOM_TXT, COL_DIM, 16);
        draw_text("Fn回车", 100, BOTTOM_TXT, k1, 16);
        draw_text("Tab撤销", 166, BOTTOM_TXT, k2, 16);
    }

    (void)bsp_display_fill(s_fb);
}

// ---- 接口 ----------------------------------------------------------------
void mic_ui_create(void)
{
    memset(s_fb, 0, sizeof(s_fb));
    s_vis_ceil = 1600;
    s_level_hold = 0;
    s_level = 0;
    build_bg();
    s_last_render = 0;
    ESP_LOGI(TAG, "UI 就绪（240x135，抖动渐变+吉祥物）");
}

void mic_ui_press(uint32_t now)
{
    if (!s_rec_start) s_rec_start = now;
    for (int i = 0; i < 30; i++) s_wave[i] = 0;
    s_wave_pos = 0;
}

void mic_ui_finished(uint32_t now, uint32_t duration_ms)
{
    (void)now; (void)duration_ms;
    s_rec_start = 0;
}

void mic_ui_action(uint32_t now, uint8_t action)
{
    s_last_action = now;
    s_last_action_kind = action == MIC_UNDO ? 1 : 2;
}

void mic_ui_update(const mic_model_t *model, uint32_t now, int peak,
                   bool audio_ok, int battery)
{
    s_model = model;
    s_audio_ok = audio_ok;
    if (battery >= 0) {
        s_battery = battery; s_battery_valid = true; s_battery_unknown = false;
    } else if (battery == MIC_BATTERY_UNKNOWN) {
        s_battery_valid = false; s_battery_unknown = true;
    } else {
        s_battery_valid = false; s_battery_unknown = false;
    }
    if (model && model->state == MIC_RECORDING) {
        feed_level(peak);
        s_wave[s_wave_pos] = s_level;
        s_wave_pos = (s_wave_pos + 1) % 30;
        if (!s_rec_start) s_rec_start = model->started;
    } else {
        s_level_hold = s_level_hold * 3 / 4;
        s_level = s_level_hold / 16;
        s_wave[s_wave_pos] = 0;          // 空闲时把历史柱排空，下次起录不带旧波形
        s_wave_pos = (s_wave_pos + 1) % 30;
    }
    render(now);
}

void mic_ui_link(bool wireless, int pairing_code)
{
    (void)wireless;
    if (pairing_code >= 0) { s_pairing = pairing_code; s_last_render = 0; }
    else if (s_pairing >= 0) { s_pairing = -1; s_last_render = 0; }
}

// ---- 启动阶段可视日志（控制台被关掉时的唯一诊断通道）----------------------
static int s_boot_n;

void mic_ui_boot_stage(const char *line1, const char *line2)
{
    s_boot_n++;
    if (s_boot_n == 1) build_bg();
    memcpy(s_fb, s_bg, sizeof(s_bg));
    char tag[12];
    snprintf(tag, sizeof(tag), "#%d", s_boot_n);
    draw_text(tag, 10, 4, COL_PURPLE, 16);
    if (line1) draw_text(line1, 16, 44, COL_CYAN, 24);
    if (line2) draw_text(line2, 16, 82, COL_FG, 16);
    (void)bsp_display_fill(s_fb);
}

void mic_ui_boot_beat(unsigned long up_s)
{
    char b[20];
    snprintf(b, sizeof(b), "up=%lus", up_s);
    draw_text(b, 16, 104, COL_MINT, 16);
    (void)bsp_display_fill(s_fb);
}

void mic_ui_bat_diag(const char *const *lines, int n)
{
    if (!lines || n <= 0) return;
    build_bg();
    memcpy(s_fb, s_bg, sizeof(s_bg));
    // 顶部细条隔开背景，6 行 16px 字（行高 18）刚好落在 8..116
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < BSP_UI_W; x++) s_fb[y * BSP_UI_W + x] = be16(COL_PURPLE);
    for (int i = 0; i < n && i < 6; i++) {
        if (!lines[i]) continue;
        draw_text(lines[i], 4, 6 + i * 18, i == 0 ? COL_AMBER : COL_FG, 16);
    }
    (void)bsp_display_fill(s_fb);
}
