// components/bsp/include/bsp_battery_soc.h
// 电池 ADC → 电压 → SOC 的纯计算层，不依赖 ESP-IDF，可由 host tests 直接编译。
//
// Cardputer ADV 没有 CW2017 电量计：电池经 2:1 分压接到 GPIO10 (ADC1_CH9)。
// 官方 UserDemo 的 Power_Class.cpp 明确 _adc_ratio = 2.0f、ADC1_GPIO10_CHANNEL。
// 本模块只做「估算」，不是电量计精度；首版用电压查表，后续可换放电曲线。
#pragma once

#include <stddef.h>
#include <stdint.h>

// ADC oneshot 采样次数（奇数，取中值）。必须与 bsp_battery.c 的采样循环一致。
#define BSP_BAT_ADC_SAMPLES 7

// 采样数组取中值（纯函数）。n 为偶数时取中间偏大的一个；非法输入返回 -1。
// 抽出来的原因：ADC 驱动调用曾在探针清理中被误删，7 次采样全填 -1 却依然能
// 编译通过、host 侧毫无察觉；把中值逻辑放到可测层后，test_bsp_battery_soc.c
// 能锁住「采样值必须参与计算」这一契约。
int bsp_battery_median_raw(const int *raw, size_t n);

// 无出厂校准时的退化换算：12bit @ 12dB 衰减，满量程约 3100mV（分压后引脚电压）。
int bsp_battery_raw_to_mv_fallback(int raw);

// ADC 校准后的采样点电压（mV，分压后）→ 电芯电压（mV）。
// ratio 由 bsp_pins.h 的 BSP_BAT_ADC_RATIO_NUM/DEN 给出，此处以参数传入保持纯函数。
int bsp_battery_mv_from_adc(int adc_mv, uint32_t ratio_num, uint32_t ratio_den);

// 电芯电压（mV）→ SOC 百分比 0..100。越界时钳位；无效输入返回 -1。
int bsp_battery_soc_from_mv(int cell_mv);
