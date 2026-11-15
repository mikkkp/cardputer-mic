#!/usr/bin/env python3
"""BLE 音频帧探针（不依赖伴测程序，只数帧 + 打日志）。

用途：固件改完先跑它确认 BLE 链路、加密订阅、帧流是否正常，不用起完整伴测。

前置：需要带 bleak 的 Python（本仓库用 passport-mic/.venv）。

    cd passport-mic
    .venv/bin/python ../cardputer-mic/tools/ble_probe.py 300

要点：
  · 设备只有在 encrypted && subscribed 之后才认为"已连接"，只在系统蓝牙里
    显示"已连接"不够；必须真的完成加密并订阅音频特征。
  · 设备 3 秒收不到 P 心跳就停流，心跳写失败要立刻重试。
  · 首帧（START）要在 2 秒内回 A，否则设备不进入流模式。
  · 控制台已全关（CONFIG_ESP_CONSOLE_NONE），设备没有串口日志，这是唯一
    能看到"帧有没有在走"的通道。
"""

import asyncio, time
from bleak import BleakScanner, BleakClient

SVC="12345678-1234-5678-1234-56789abcdef0"
CTRL="12345678-1234-5678-1234-56789abcdef2"
AUDIO="12345678-1234-5678-1234-56789abcdef1"

async def ping(c):
    while True:
        await asyncio.sleep(0.5)
        for _ in range(2):                      # 写失败立即重试，保住 3s 心跳窗
            try:
                await c.write_gatt_char(CTRL, b"P", response=True); break
            except Exception as e:
                print("ping retry", flush=True); await asyncio.sleep(0.15)

async def main():
    seen=[]
    def cb(d,adv):
        if SVC in [str(u).lower() for u in (adv.service_uuids or [])]: seen.append((d,adv))
    print("scan...", flush=True)
    async with BleakScanner(detection_callback=cb):
        t0=time.time()
        while time.time()-t0 < 60:
            if seen: break
            await asyncio.sleep(1)
    if not seen:
        print("NO ADVERTISEMENT", flush=True); return
    d=seen[0][0]
    print("FOUND", d.name, flush=True)
    st={"n":0,"b":0,"max":0,"audio":0,"abytes":0,"t0":None,"tlast":0}
    async with BleakClient(d, timeout=20) as c:
        print("CONNECTED", flush=True)
        print("CTRL =", await c.read_gatt_char(CTRL), flush=True)
        async def on_audio(_, data):
            st["n"]+=1; st["b"]+=len(data); st["max"]=max(st["max"],len(data))
            if len(data)>60:
                if st["t0"] is None:
                    st["t0"]=time.time(); print("AUDIO START", flush=True)
                st["audio"]+=1; st["abytes"]+=len(data); st["tlast"]=time.time()
            try: await c.write_gatt_char(CTRL, b"A", response=True)
            except Exception: pass
        await c.start_notify(AUDIO, on_audio)
        print("SUBSCRIBED — 等 OK 键", flush=True)
        p=asyncio.create_task(ping(c))
        t0=time.time()
        while time.time()-t0 < 300:
            await asyncio.sleep(2)
            if st["audio"] and st["audio"] % 15 == 0 and st["t0"]:
                print(f"PROGRESS audio={st['audio']} abytes={st['abytes']} max={st['max']} dur={st['tlast']-st['t0']:.1f}s", flush=True)
            if st["audio"] >= 150:
                print("ENOUGH", flush=True); break
        p.cancel()
        dur=(st["tlast"]-st["t0"]) if st["t0"] else 0
        print(f"SUMMARY frames={st['n']} bytes={st['b']} max={st['max']} audio={st['audio']} abytes={st['abytes']} dur={dur:.1f}s", flush=True)
asyncio.run(main())
