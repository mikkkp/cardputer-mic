#!/usr/bin/env python3
"""P3-3 观测：BLE 音频帧连续性 + POWER 遥测（电量、USB 标志、最低空闲堆）。

与 ble_probe.py 的区别：这里按 FPM1 序列号逐帧核对，能报出丢帧、帧类型、
POWER 里的 soc/mv/usb 以及协议保留字节携带的最低空闲堆（KB）。

前置：passport-mic/.venv（含 bleak）。用法：

    cd passport-mic
    .venv/bin/python ../cardputer-mic/tools/p3_observe.py 40

设备端需要人工按 OK 开始录音、再按 OK 结束；本脚本负责 START 回 A 与
500ms 心跳 P，不注入按键、不写 BlackHole，纯观测帧流。
"""

import asyncio
import json
import struct
import sys
import time
import zlib
from pathlib import Path

from bleak import BleakScanner, BleakClient

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "passport-mic" / "companion"))
from protocol import (  # noqa: E402
    BleFrameDecoder, MAGIC, HELLO, START, AUDIO, STOP, AUDIO_ADPCM, POWER,
    STATES, SCREENS,
)

SVC = "12345678-1234-5678-1234-56789abcdef0"
CTRL = "12345678-1234-5678-1234-56789abcdef2"
AUDIO_CHAR = "12345678-1234-5678-1234-56789abcdef1"


def decode_fpm1(raw):
    """返回 (kind, sequence, payload) 或 None。"""
    if len(raw) < 16 or not raw.startswith(MAGIC):
        return None
    kind, reserved, size, sequence, crc = struct.unpack_from("<BBHII", raw, 4)
    if reserved or len(raw) != 16 + size:
        return None
    payload = raw[16:]
    if zlib.crc32(raw[4:12] + payload) != crc:
        return None
    return kind, sequence, payload


def decode_power(payload):
    if len(payload) != 8:
        return {}
    soc, mv, state, screen, flags = struct.unpack_from("<HHBBB", payload)
    return {
        "soc": None if soc == 0xFFFF else round(soc / 256, 2),
        "mv": None if mv == 0xFFFF else mv,
        "state": STATES.get(state, str(state)),
        "screen": SCREENS.get(screen, str(screen)),
        "usb": bool(flags & 1),
        "heap_kb": payload[7],  # 协议保留位，Mac 端 parse_power 显式跳过
    }


async def ping(client, alive):
    while alive[0]:
        await asyncio.sleep(0.5)
        for _ in range(2):
            try:
                await client.write_gatt_char(CTRL, b"P", response=True)
                break
            except Exception:
                await asyncio.sleep(0.15)


async def main():
    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 75
    t_start = time.time()  # 回调里会引用，先初始化

    found = []

    def on_scan(dev, adv):
        if SVC in [str(u).lower() for u in (adv.service_uuids or [])]:
            found.append(dev)

    print("scan...", flush=True)
    async with BleakScanner(detection_callback=on_scan):
        t0 = time.time()
        while time.time() - t0 < 60 and not found:
            await asyncio.sleep(1)
    if not found:
        print("NO ADVERTISEMENT", flush=True)
        return
    print("FOUND", found[0].name, flush=True)

    st = {
        "frames": 0, "audio": 0, "abytes": 0, "hello": 0, "power": [],
        "gaps": 0, "max_len": 0, "bad_frag": 0, "bad_crc": 0,
        "last_seq": None, "t_first": None, "t_last": None,
        "start_at": None, "stop_at": None,
    }
    dec = BleFrameDecoder()
    alive = [True]

    # 纯电池冷启动后首次配对要人工输 6 位码，连接超时留足时间。
    async with BleakClient(found[0], timeout=120) as client:
        print("CONNECTED", flush=True)
        print("CTRL =", await client.read_gatt_char(CTRL), flush=True)

        async def on_notify(_, data):
            st["max_len"] = max(st["max_len"], len(data))
            try:
                raws = dec.feed(data)
            except ValueError:
                st["bad_frag"] += 1
                return
            for raw in raws:
                got = decode_fpm1(raw)
                if got is None:
                    st["bad_crc"] += 1
                    continue
                kind, seq, payload = got
                st["frames"] += 1
                if st["last_seq"] is not None and seq != (st["last_seq"] + 1) & 0xFFFFFFFF:
                    st["gaps"] += 1
                st["last_seq"] = seq
                if kind in (AUDIO, AUDIO_ADPCM):
                    if st["t_first"] is None:
                        st["t_first"] = time.time()
                        print("AUDIO START", flush=True)
                    st["t_last"] = time.time()
                    st["audio"] += 1
                    st["abytes"] += len(payload)
                elif kind == HELLO:
                    st["hello"] += 1
                elif kind == START:
                    st["start_at"] = time.time()
                    print("START -> A", flush=True)
                    try:
                        await client.write_gatt_char(CTRL, b"A", response=True)
                    except Exception as exc:
                        print("ACK FAIL", exc, flush=True)
                elif kind == STOP:
                    st["stop_at"] = time.time()
                    print("STOP", flush=True)
                elif kind == POWER:
                    p = decode_power(payload)
                    p["t"] = round(time.time() - t_start, 1)
                    st["power"].append(p)
                    print("POWER", json.dumps(p, ensure_ascii=False), flush=True)

        await client.start_notify(AUDIO_CHAR, on_notify)
        print("SUBSCRIBED — 请按 OK 开始录音", flush=True)
        t_start = time.time()  # 计时从订阅成功起算
        pinger = asyncio.create_task(ping(client, alive))

        while time.time() - t_start < duration:
            await asyncio.sleep(2)
            el = time.time() - t_start
            print(f"PROGRESS t={el:.0f}s frames={st['frames']} audio={st['audio']} "
                  f"hello={st['hello']} gaps={st['gaps']}", flush=True)
        alive[0] = False
        pinger.cancel()

    heaps = [p["heap_kb"] for p in st["power"] if "heap_kb" in p]
    summary = {
        "wall_s": round(time.time() - t_start, 1),
        "audio_s": round((st["t_last"] - st["t_first"]) if st["t_first"] else 0, 1),
        "frames": st["frames"], "audio": st["audio"], "abytes": st["abytes"],
        "hello": st["hello"], "seq_gaps": st["gaps"],
        "bad_frag": st["bad_frag"], "bad_crc": st["bad_crc"],
        "max_notify": st["max_len"],
        "power_reports": len(st["power"]),
        "heap_kb_first": heaps[0] if heaps else None,
        "heap_kb_last": heaps[-1] if heaps else None,
        "heap_kb_min": min(heaps) if heaps else None,
        "soc": st["power"][-1].get("soc") if st["power"] else None,
        "mv": st["power"][-1].get("mv") if st["power"] else None,
        "usb": st["power"][-1].get("usb") if st["power"] else None,
    }
    print("SUMMARY " + json.dumps(summary, ensure_ascii=False), flush=True)


asyncio.run(main())
