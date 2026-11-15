// tests/test_ui_render.c
// 主机侧 UI 预览：把 mic_ui_stub.c 的渲染结果输出成 PPM，用于校对字形/布局/配色，
// 不必反复烧录。构建时用一个假的 bsp_display_fill() 接收完整帧缓冲。
//
// 用法：./ui_preview <out.ppm> [state] [battery] [pairing]
//   state: 0=OFFLINE 1=READY 2=STARTING 3=RECORDING(小声) 4=RECORDING(静音)
//   battery: >=0 百分比；-1 USB 插头；-2 电量未知（"--%"）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "bsp_display.h"   // shim：BSP_UI_W/H + esp_err_t
#include "mic_ui_stub.h"

// 由 mic_ui_stub.c 调用；把帧缓冲落盘（同时做回读字节交换，恢复逻辑色序）
void ui_capture(const uint16_t *fb, int w, int h);

static const char *g_out;
static uint16_t g_buf[240 * 135];

void ui_capture(const uint16_t *fb, int w, int h)
{
    memcpy(g_buf, fb, sizeof(g_buf));
    FILE *f = fopen(g_out, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint16_t c = (uint16_t)((g_buf[i] >> 8) | (g_buf[i] << 8));  // 抵消显示字节序
        int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        unsigned char px[3] = {
            (unsigned char)((r * 255 + 15) / 31),
            (unsigned char)((g * 255 + 31) / 63),
            (unsigned char)((b * 255 + 15) / 31),
        };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s out.ppm [state] [battery] [pairing]\n", argv[0]); return 2; }
    g_out = argv[1];
    int state = argc > 2 ? atoi(argv[2]) : 1;
    int battery = argc > 3 ? atoi(argv[3]) : 78;
    int pairing = argc > 4 ? atoi(argv[4]) : -1;

    mic_model_t model;
    memset(&model, 0, sizeof(model));
    model.state = state == 4 ? MIC_RECORDING : (mic_state_t)state;
    model.started = 0;

    mic_ui_create();
    mic_ui_link(true, pairing);
    if (pairing >= 0) {
        mic_ui_update(&model, 1000, 0, true, battery);
    } else if (state == 3 || state == 4) {
        // 用真机常见的小电平喂数据：正常说话 peak 只有几百到几千，
        // 旧版按 30000 满量程映射时这些值全部只画出 1~3px（用户反馈"波动不明显"）。
        model.started = 1000;
        mic_ui_press(1000);
        for (int i = 0; i < 30; i++) {
            int peak = state == 4 ? 90 : 250 + (i % 7) * 300 + (i % 3) * 80;
            mic_ui_update(&model, (uint32_t)(1000 + i * 60 + 60), peak, true, battery);
        }
        mic_ui_update(&model, 13000, state == 4 ? 90 : 1400, true, battery);
    } else {
        mic_ui_update(&model, 1000, 0, true, battery);
    }
    printf("wrote %s (state=%d battery=%d pairing=%d)\n", g_out, state, battery, pairing);
    return 0;
}

// 帧提交入口：micron 的 mic_ui_stub.c 每帧调用
esp_err_t bsp_display_fill(const uint16_t *frame)
{
    ui_capture(frame, 240, 135);
    return 0;
}
