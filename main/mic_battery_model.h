// main/mic_battery_model.h
// 库仑计数电量模型（2026-11-15 定版，方案 A）。
//
// 背景：Cardputer ADV 的 SAR ADC 在任何协议下都读不出可信电池电压
// （P3-3 + BATDIAG v1~v5 交叉验证：BLE 后掉到 680、reinit 无效、
//   每次 read 损伤模拟域、同协议不可复现）。硬件侧唯一可靠方案是
// 外接 I2C 电量计；本模型是零硬件改动的软件估算。
//
// 原理：
//   · NVS 持久化剩余容量（mAh），跨重启保持。
//   · 纯电池运行：按估算电流消耗 mAh（录音 > 常亮 > 待机三档）。
//   · USB 在场：视为充电中，按充电电流回充 mAh，到满截止。
//   · USB 插满超过 CHG_HOLD_S 秒 → 校准到满容量（TP4057 CV 段进入 trickle）。
//
// 精度：±10~15% 量级，取决于实际电流与标称 1500mAh 的偏差。
// 显示语义：0..100 有效百分比；USB 在场且未判满时 UI 侧仍用插头图标。
#ifndef MIC_BATTERY_MODEL_H
#define MIC_BATTERY_MODEL_H

#include <stdbool.h>
#include <stdint.h>

// 初始化：从 NVS 恢复剩余容量。NVS 无记录时按 50% 起步（保守）。
void mic_battery_model_init(void);

// 主循环驱动：每秒调用一次。
//   usb_connected = usb_serial_jtag_is_connected()
//   recording     = 当前处于录音状态
//   screen_on     = 屏幕非 OFF（DIM 也算，背光只剩漏电流时用 false）
void mic_battery_model_tick(bool usb_connected, bool recording, bool screen_on);

// SOC 0..100；异常返回 -1。
int mic_battery_model_soc(void);

// SOC 的 Q8 表示（0..100*256）；异常返回 -1。
int mic_battery_model_soc_q8(void);

// 剩余 mAh（0..1500）。
int mic_battery_model_mah(void);

// USB 插入且已判定「充电完成」（持续在充电状态超过 CHG_HOLD_S）。
bool mic_battery_model_charge_full(void);

#endif  // MIC_BATTERY_MODEL_H
