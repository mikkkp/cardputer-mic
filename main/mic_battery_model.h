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
//   · 活跃使用（亮屏/录音）：按估算电流扣减 mAh（录音 > 常亮 > 待机三档）。
//   · 充电判定（2026-11-18 v2）：不用 USB 信号。usb_serial_jtag_is_connected()
//     检测的是主机 SOF 帧包，充电头/充电宝不是 USB 主机（无 SOF），插 Mac 也
//     常因端口挂起无 SOF，实测恒 false → 充电从不累计。改为「静默推断」：
//     屏幕熄灭且未录音连续超过 QUIET_ASSUME_CHARGE_S 秒 → 认为设备放在
//     充电器上（用户使用习惯：不在手里用的时候基本都在充电），按充电电流
//     回充；累计回充超过 CHG_HOLD_S 秒 → 校准到满容量。
//     已知误差：长时间熄屏搁置（未插电）也会被当作充电，电量会偏高；
//     用户充电频率高，接受此折中。
//
// 精度：软件推断，仅供显示参考。
// 显示语义：0..100 有效百分比。
#ifndef MIC_BATTERY_MODEL_H
#define MIC_BATTERY_MODEL_H

#include <stdbool.h>
#include <stdint.h>

// 初始化：从 NVS 恢复剩余容量。NVS 无记录时按 50% 起步（保守）。
void mic_battery_model_init(void);

// 主循环驱动：每秒调用一次。
//   recording = 当前处于录音状态
//   screen_on = 屏幕非 OFF（DIM 也算亮屏）
// 充电判定在模型内部：熄屏静置 ≥ QUIET_ASSUME_CHARGE_S 视为充电中。
void mic_battery_model_tick(bool recording, bool screen_on);

// SOC 0..100；异常返回 -1。
int mic_battery_model_soc(void);

// SOC 的 Q8 表示（0..100*256）；异常返回 -1。
int mic_battery_model_soc_q8(void);

// 剩余 mAh（0..1500）。
int mic_battery_model_mah(void);

// USB 插入且已判定「充电完成」（持续在充电状态超过 CHG_HOLD_S）。
bool mic_battery_model_charge_full(void);

#endif  // MIC_BATTERY_MODEL_H
