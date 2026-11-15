// components/bsp/include/bsp_battery.h
// 电池采集：GPIO10 = ADC1_CH9，2:1 分压（官方 Power_Class.cpp 实测）。
// 纯计算层在 bsp_battery_soc.h（host tests 可编译）；本文件是 ESP-IDF ADC 侧。
#pragma once

#include "esp_err.h"

// 初始化 ADC1 通道。幂等。
esp_err_t bsp_battery_init(void);

// 电芯电压 mV；读失败返回 -1。
int bsp_battery_mv(void);

/* ---- 一次性多档探针（仅诊断固件用，不参与正式流程）----------------------
   同一次采样用「衰减 × 校准算法」四种组合各读一遍，用于判断读数偏差
   来自分压比、还是来自 DB_12 档的校准外推。字段用 int 避免头文件依赖
   IDF 的 adc 枚举，host 侧也能包含。 */
typedef struct {
    int raw;      // 7 次采样的中值原始读数
    int mv;       // 出厂曲线校准换算的引脚电压；校准不可用为 -1
    int atten;    // 12 = DB_12，6 = DB_6
    int lin_full; // 该档标称满量程 mV（DB_12=3100，DB_6=1750），线性估算 raw*lin_full/4095
} bsp_bat_probe_t;

// 依次填充 4 档结果（DB12/curve, DB12/line, DB6/curve, DB6/line），返回实际档数。
// v2 实测：切档后同档复读被拖低近一半，此路径仅作历史参考。
int bsp_battery_probe_all(bsp_bat_probe_t *out, int max);

/* ---- 单档顺序探针（v5，电池调查专用）-----------------------------------
   v4 结论：每次 adc_oneshot_read 本身就会损伤模拟域且不可逆。
     S0(0ms)  = 2515（init 后还没恢复完就抢读，偏低）
     S1(300ms) =  767（S0 的读取把域打掉了）
     S2(600ms) =  683
     S3(900ms) =  670（到底）
   昨天 DIAG2：init → 300ms 静置 → 只读一组 → 2931/2934（可复现 0.1%）。
   v5 就用这个协议做满电定标，之后不再读取。 */
int bsp_battery_probe_seq(int *raw_out, int *mv_out, int count);

// SOC 0..100（由 bsp_battery_soc_from_mv 查表）；读失败返回 -1。
int bsp_battery_soc(void);

// SOC 的 1/256 %（用于 MIC_POWER 帧）；读失败返回 -1。
int bsp_battery_soc_q8(void);
