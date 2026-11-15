#!/usr/bin/env python3
"""P3-3 电量校准报告：把实测电芯电压与固件 SOC 查表曲线对齐。

用法：
    python3 tools/p3_battery_report.py                 # 读 battery.csv 最近 N 条
    python3 tools/p3_battery_report.py 4100 3980 3850  # 直接传实测 mV

固件曲线（components/bsp/src/bsp_battery_soc.c）是 Li-ion 典型放电的粗估锚点，
本脚本只做「实测点是否符合预期」的检查，不自动改曲线；改曲线需要跨多个电量
水平、数小时的放电数据，并同步更新 host test 的锚点断言。
"""

import csv
import sys
from datetime import datetime
from pathlib import Path

CURVE = [(3300, 0), (3500, 20), (3700, 45), (3900, 75), (4200, 100)]
LOG = Path.home() / "Library/Logs/PassportMic/battery.csv"


def soc_of(mv):
    if mv <= 0:
        return -1
    if mv <= CURVE[0][0]:
        return 0
    for i in range(1, len(CURVE)):
        if mv <= CURVE[i][0]:
            lo, hi = CURVE[i - 1], CURVE[i]
            span, delta, off = hi[0] - lo[0], hi[1] - lo[1], mv - lo[0]
            return lo[1] + (off * delta + span // 2) // span
    return 100


def main():
    if len(sys.argv) > 1:
        samples = [(f"arg{i}", int(v), None) for i, v in enumerate(sys.argv[1:], 1)]
    else:
        if not LOG.exists():
            print(f"找不到 {LOG}")
            return
        with LOG.open(newline="") as handle:
            rows = list(csv.DictReader(handle))
        samples = []
        for row in rows[-200:]:
            if row.get("mv") and row.get("usb", "").lower() == "false":
                samples.append((row["time"], int(row["mv"]), row.get("soc")))
        if not samples:
            print("battery.csv 里没有 usb=False 且带 mV 的记录：说明 ADC 还没启用，"
                  "需要在纯电池供电下冷启动。")
            return

    print(f"{'时间':<28}{'mV':>7}{'固件SOC':>10}{'期望SOC':>10}{'偏差':>8}")
    for t, mv, reported in samples:
        expect = soc_of(mv)
        got = "-" if reported in (None, "") else f"{float(reported):.2f}"
        diff = "-" if reported in (None, "") else f"{float(reported) - expect:+.2f}"
        print(f"{str(t):<28}{mv:>7}{got:>10}{expect:>10}{diff:>8}")
    print()
    print("判读：")
    print("  · 满电静置应在 4050~4200mV，对应 SOC 88~100%。")
    print("  · ADC 分压 2:1，若读数只有 ~2050mV 说明漏乘 ratio；若 >4400mV 说明重复乘了。")
    print("  · 偏差 >5% 且整段单调偏移，才考虑调整 k_curve 锚点（需跨电量水平数据）。")
    print(f"  · 报告生成 {datetime.now().astimezone().isoformat(timespec='seconds')}")


if __name__ == "__main__":
    main()
