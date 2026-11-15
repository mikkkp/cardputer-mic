#!/usr/bin/env python3
"""P4 验收探针：按 START/STOP 分段统计、ADPCM 解码电平（peak/RMS）、断连恢复计时。

与 p3_observe.py 的区别：
- 按 OK 产生的每对 START/STOP 记为一段，逐段输出 SEG 行
  （audio_s / frames / bytes / seq_gaps / bad / peak / rms / 缺帧率）；
- 音频 payload 按 164B IMA ADPCM block 解码成 PCM 后计算段内峰值与 RMS，
  支持 1/5/15 cm 电平三档实测（每档录一段即可）；
- reconnect 子命令：主动断开 → 计时到广播恢复 → 重连 → 验证 READY，
  连做 3 轮取最差值（验收判据 5s 内恢复）。

前置：passport-mic/.venv（含 bleak）。用法：

    cd passport-mic
    .venv/bin/python ../cardputer-mic/tools/p4_probe.py segments 900
    .venv/bin/python ../cardputer-mic/tools/p4_probe.py reconnect
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
    BleFrameDecoder, MAGIC, HELLO, START, AUDIO, AUDIO_ADPCM, STOP, POWER,
    STATES, SCREENS,
)
from adpcm import decode as adpcm_decode  # noqa: E402

SVC = "12345678-1234-5678-1234-56789abcdef0"
CTRL = "12345678-1234-5678-1234-56789abcdef2"
AUDIO_CHAR = "12345678-1234-5678-1234-56789abcdef1"


def decode_fpm1(raw):
    """返回 (kind, sequence, payload) 或 None（长度/保留位/CRC 任一不过）。"""
    if len(raw) < 16 or not raw.startswith(MAGIC):
        return None
    kind, reserved, size, sequence, crc = struct.unpack_from("<BBHII", raw, 4)
    if reserved or len(raw) != 16 + size:
        return None
    payload = raw[16:]
    if zlib.crc32(raw[4:12] + payload) != crc:
        return None
    return kind, sequence, payload


async def find_device(timeout=60):
    found = []

    def on_scan(dev, adv):
        if SVC in [str(u).lower() for u in (adv.service_uuids or [])]:
            found.append(dev)

    async with BleakScanner(detection_callback=on_scan):
        t0 = time.time()
        while time.time() - t0 < timeout and not found:
            await asyncio.sleep(0.5)
    return found[0] if found else None


class Segmentor:
    """把帧流按 START/STOP 切段并统计电平。"""

    last_hello_state = None

    def __init__(self):
        self.segs = []
        self.cur = None
        self.last_seq = None
        self.last_seq_t = None
        self.total_bad = 0

    def feed(self, kind, seq, payload, now):
        if self.last_seq is not None and seq != (self.last_seq + 1) & 0xFFFFFFFF:
            if self.cur is not None:
                self.cur["gaps"] += 1
            else:
                self.total_bad += 1
        self.last_seq = seq
        self.last_seq_t = now

        if kind == START:
            self.cur = self.new_seg(now)
        elif kind == STOP:
            if self.cur is not None:
                self.close_seg(now)
        elif kind in (AUDIO, AUDIO_ADPCM) and self.cur is not None:
            # 录音期间零计算：只缓存 payload，段结束后再解码算电平，
            # 避免 on_notify 回调阻塞事件循环导致心跳 P 发不出去（3s 超时停流）
            c = self.cur
            if c["t_first"] is None:
                c["t_first"] = now
            c["t_last"] = now
            c["frames"] += 1
            c["bytes"] += len(payload)
            c["chunks"].append(bytes(payload))

    @staticmethod
    def new_seg(now):
        return {
            "t0": now, "t_first": None, "t_last": None,
            "frames": 0, "bytes": 0, "gaps": 0, "bad_blocks": 0,
            "peak": 0, "sq": 0, "n": 0, "chunks": [],
        }

    def close_seg(self, now):
        c = self.cur
        # 段结束后统一解码：从缓存 payload 逐 block 算 peak/RMS
        for chunk in c["chunks"]:
            for off in range(0, len(chunk) - 163, 164):
                try:
                    pcm = adpcm_decode(chunk[off:off + 164])
                except ValueError:
                    c["bad_blocks"] += 1
                    continue
                samples = struct.unpack("<320h", pcm)
                peak = max(max(samples), -min(samples))
                c["peak"] = max(c["peak"], peak)
                c["sq"] += sum(s * s for s in samples)
                c["n"] += len(samples)
        del c["chunks"]
        audio_s = (c["t_last"] - c["t_first"]) if c["t_last"] else 0
        # 段内实际音频窗口以首帧到末帧为准（不含 START->首帧、末帧->STOP 的边界）；
        # 50 帧/秒，容忍固定边界误差 4 帧 + 0.5% 抖动
        expected = round(audio_s * 50) + 1
        miss = max(0, expected - c["frames"])
        rms = int((c["sq"] / c["n"]) ** 0.5) if c["n"] else 0
        seg = {
            "idx": len(self.segs) + 1,
            "audio_s": round(audio_s, 1),
            "frames": c["frames"],
            "bytes": c["bytes"],
            "expected": expected,
            "missing": miss,
            "missing_ok": miss <= 4 + int(expected * 0.005),
            "seq_gaps": c["gaps"],
            "bad_blocks": c["bad_blocks"],
            "peak": c["peak"],
            "rms": rms,
        }
        self.segs.append(seg)
        self.cur = None
        return seg

    def flush(self, now):
        if self.cur is not None:
            return self.close_seg(now)
        return None


async def write_ctrl(client, payload, tag, write_lock, stats):
    """串行化控制写入；带 2s 超时，>0.5s 记录耗时抓心跳卡顿证据。"""
    async with write_lock:
        t0 = time.time()
        try:
            await asyncio.wait_for(client.write_gatt_char(CTRL, payload, response=True), 2.0)
            dt = time.time() - t0
            if dt > 0.5:
                stats["slow_writes"].append((tag, round(dt, 2)))
                print(f"SLOWWRITE {tag} {dt:.2f}s", flush=True)
            stats["ok_writes"] += 1
        except Exception as exc:
            stats["fail_writes"] += 1
            print(f"WRITEFAIL {tag} {exc}", flush=True)


async def ping(client, alive, write_lock, stats):
    """心跳：0.25s 间隔，协议 3s 超时留 12 倍余量。"""
    while alive[0]:
        await asyncio.sleep(0.25)
        await write_ctrl(client, b"P", "P", write_lock, stats)


async def segments_mode(duration):
    dev = await find_device()
    if not dev:
        print("NO ADVERTISEMENT", flush=True)
        return 1
    print("FOUND", dev.name, flush=True)

    seg = Segmentor()
    alive = [True]
    write_lock = asyncio.Lock()
    wstats = {"ok_writes": 0, "fail_writes": 0, "slow_writes": []}
    t_start = time.time()

    async with BleakClient(dev, timeout=120) as client:
        print("CONNECTED", flush=True)
        print("CTRL =", await client.read_gatt_char(CTRL), flush=True)
        dec = BleFrameDecoder()

        async def on_notify(_, data):
            try:
                raws = dec.feed(data)
            except ValueError:
                return
            now = time.time()
            for raw in raws:
                got = decode_fpm1(raw)
                if got is None:
                    continue
                kind, seq, payload = got
                seg.feed(kind, seq, payload, now)
                if kind == START:
                    print("START -> A（回 ACK）", flush=True)
                    await write_ctrl(client, b"A", "A", write_lock, wstats)
                elif kind == STOP:
                    s = seg.segs[-1] if seg.segs else None
                    print("STOP -> SEG " + json.dumps(s, ensure_ascii=False), flush=True)
                elif kind == HELLO:
                    if payload and payload[0] != seg.last_hello_state:
                        seg.last_hello_state = payload[0]
                        print(f"STATE -> {payload[0]} (1=READY 0=OFFLINE)", flush=True)

        await client.start_notify(AUDIO_CHAR, on_notify)
        print("SUBSCRIBED — 请按 OK 开始录音，再按 OK 结束一段；可连续多段", flush=True)
        t_start = time.time()
        pinger = asyncio.create_task(ping(client, alive, write_lock, wstats))

        while time.time() - t_start < duration:
            await asyncio.sleep(5)
            el = time.time() - t_start
            print(f"PROGRESS t={el:.0f}s segs={len(seg.segs)} last_seq={seg.last_seq}", flush=True)
            if seg.last_seq is not None and el - (seg.last_seq_t or 0) > 30:
                print("WARN no frames for 30s — link may be down", flush=True)
        alive[0] = False
        pinger.cancel()
        seg.flush(time.time())

    ok = bool(seg.segs) and all(
        s["seq_gaps"] == 0 and s["bad_blocks"] == 0 and s["missing_ok"]
        for s in seg.segs
    )
    summary = {
        "segments": len(seg.segs),
        "total_audio_s": round(sum(s["audio_s"] for s in seg.segs), 1),
        "total_frames": sum(s["frames"] for s in seg.segs),
        "total_missing": sum(s["missing"] for s in seg.segs),
        "worst_missing": max((s["missing"] for s in seg.segs), default=0),
        "seq_gaps": sum(s["seq_gaps"] for s in seg.segs),
        "bad_blocks": sum(s["bad_blocks"] for s in seg.segs),
        "peak_max": max((s["peak"] for s in seg.segs), default=0),
        "writes": wstats["ok_writes"],
        "write_fails": wstats["fail_writes"],
        "slow_writes": wstats["slow_writes"][-6:],
        "verdict": "PASS" if ok else "FAIL",
    }
    print("P4SUMMARY " + json.dumps(summary, ensure_ascii=False), flush=True)
    return 0 if ok else 2


async def reconnect_mode(rounds=3):
    results = []
    for idx in range(1, rounds + 1):
        dev = await find_device()
        if not dev:
            print(f"ROUND {idx} NO ADVERTISEMENT", flush=True)
            return 1
        t0 = time.time()
        async with BleakClient(dev, timeout=30) as client:
            dec = BleFrameDecoder()
            ready = []

            async def on_notify(_, data):
                try:
                    raws = dec.feed(data)
                except ValueError:
                    return
                for raw in raws:
                    got = decode_fpm1(raw)
                    if got and got[0] == HELLO:
                        ready.append((time.time(), got[2][0] if got[2] else None))

            await client.start_notify(AUDIO_CHAR, on_notify)
            await asyncio.sleep(2)  # 等加密/订阅稳定 + 至少一帧 HELLO
        t_disc = time.time()  # 离开 with 即主动断开

        # 从断开起计时：等设备重新出现在广播里
        dev2 = await find_device(timeout=15)
        t_adv = time.time() if dev2 else None

        # 立即重连并等 READY（HELLO 载荷 01）
        ready_at = None
        state = None
        if dev2:
            try:
                async with BleakClient(dev2, timeout=15) as client:
                    dec = BleFrameDecoder()
                    done = asyncio.Event()

                    async def on_notify2(_, data):
                        nonlocal ready_at, state
                        try:
                            raws = dec.feed(data)
                        except ValueError:
                            return
                        for raw in raws:
                            got = decode_fpm1(raw)
                            if not got:
                                continue
                            kind, _seq, payload = got
                            if kind == HELLO:
                                if payload and payload[0] == 1:
                                    ready_at = time.time()
                                    done.set()
                                continue  # HELLO 不再触发 done，只等 READY 或记 POWER state
                            if kind == POWER and len(payload) == 8:
                                _soc, _mv, st, _sc, _fl = struct.unpack_from("<HHBBB", payload)
                                state = STATES.get(st, str(st))
                                if ready_at is None:
                                    ready_at = time.time()  # POWER 状态帧也能证明链路恢复
                                done.set()

                    await client.start_notify(AUDIO_CHAR, on_notify2)
                    # HELLO 是对心跳 P 的响应：不发 P 等不到 READY 帧
                    await client.write_gatt_char(CTRL, b"P", response=True)
                    try:
                        await asyncio.wait_for(done.wait(), timeout=10)
                    except asyncio.TimeoutError:
                        pass
            except Exception as exc:
                print("RECONNECT FAIL", exc, flush=True)

        r = {
            "round": idx,
            "connect_s": round(t_disc - t0, 2),
            "adv_after_disc_s": round(t_adv - t_disc, 2) if t_adv else None,
            "ready_after_disc_s": round(ready_at - t_disc, 2) if ready_at else None,
            "state": state,
        }
        results.append(r)
        print("ROUND " + json.dumps(r, ensure_ascii=False), flush=True)
        await asyncio.sleep(1)

    worst = max((r["ready_after_disc_s"] or 99) for r in results)
    summary = {
        "rounds": len(results),
        "worst_ready_s": round(worst, 2),
        "verdict": "PASS" if worst <= 5.0 else "FAIL",
    }
    print("P4SUMMARY " + json.dumps(summary, ensure_ascii=False), flush=True)
    return 0 if worst <= 5.0 else 2


async def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "segments"
    if mode == "reconnect":
        return await reconnect_mode()
    dur = int(sys.argv[2]) if len(sys.argv) > 2 else 900
    return await segments_mode(dur)


sys.exit(asyncio.run(main()))
