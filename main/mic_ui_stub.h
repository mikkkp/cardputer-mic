// main/mic_ui_stub.h
// P2 占位 UI：保持上游 mic_ui.h 接口签名不变，内部换成裸 framebuffer 绘制。
// 用 5x7 像素字体显示状态短词与六位配对码；P3 再按 §4.8 重写正式 UI。
#pragma once
#include "mic_protocol.h"

/* mic_ui_update() 的 battery 参数：
     >= 0  有效 SOC 百分比，画数字 + 填充电池图标
     -1    USB 供电（或 ADC 未启用），画插头图标
     -2    电池供电但读数不可信，画 "--%" + 空电池壳
   负值语义由本头文件定义，避免 magic number 散落。 */
#define MIC_BATTERY_USB      (-1)
#define MIC_BATTERY_UNKNOWN  (-2)

void mic_ui_create(void);
void mic_ui_press(uint32_t now);
void mic_ui_finished(uint32_t now, uint32_t duration_ms);
void mic_ui_update(const mic_model_t *model, uint32_t now, int peak,
                   bool audio_ok, int battery);

void mic_ui_link(bool wireless, int pairing_code);
void mic_ui_action(uint32_t now, uint8_t action);

// P3 诊断：屏幕可视化日志（串口不可用时定位卡死位置）
void mic_ui_boot_stage(const char *line1, const char *line2);
void mic_ui_boot_beat(unsigned long up_s);

// 电池调查诊断：把多行文本直接铺满屏幕（仅诊断固件使用）。
// lines 为 NULL 或 n<=0 时只重绘背景。
void mic_ui_bat_diag(const char *const *lines, int n);
