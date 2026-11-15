# Cardputer Mic（Cardputer 蓝牙话筒）

**English** · [简体中文](README.zh_CN.md)

把 [Passport Mic](https://github.com/MatchlessHeroHx/ai-passport-mic) 蓝牙语音话筒
移植到 M5Stack Cardputer ADV 的固件（上游 MIT）。BLE 协议与 Mac 端 companion
零改动：`BLE → BlackHole 2ch → 豆包输入法`。

**状态：可日常使用（P0–P4 全部完成）。** 核心功能已在真机验收。上游归属见
[NOTICE](NOTICE)。

## 功能

按 **Ctrl** 键（左下角最底）对 Mac 上任意文本框听写。固件通过 BLE 以
16 kHz / 16-bit 单声道 IMA-ADPCM 流式传输音频，Mac 端 companion 送入豆包
输入法做语音转文字。

| 按键（丝印） | 功能 |
|---|---|
| **Ctrl**（最底） | 听写（开始 / 结束录音） |
| **Fn**（中间） | 回车 / 确认 |
| **Tab**（最上） | 撤销上次听写 |

电量百分比采用库仑计数估算（NVS 持久化，精度 ±10~15%），显示在右上角。
插 USB-C 充电 1.5 小时后自动校准到 100%。

## 硬件事实

Stamp-S3A / ESP32-S3FN8，8 MB Flash，**无 PSRAM**，240×135 ST7789V2，
56 键 TCA8418，ES8311 + NS4150B。GPIO10 上的电池 SAR ADC 在这块板
**不可用**（模拟域与 BT PHY / USB-JTAG 共享，init 后毫秒级失真），
改用软件库仑计数估算电量。

权威引脚表在 [`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h)，
由 `tests/test_bsp_pins.py` 检查。

关键值：内部 I2C `G8/G9`；ES8311 I2S `SCLK=G41`、`ASDOUT→MCU=G46`、
`LRCK=G43`、`DSDIN←MCU=G42`，**无独立 MCLK**（官方驱动 `MCLK=BCLK`）；
TCA8418 INT `G11`；`G38` 为 `DISP_BL` 兼 RGB LED `PWR_EN`。

## 构建

```bash
source ~/esp/esp-idf/export.sh
./tools/validate.sh --static      # host 测试，无需工具链
./tools/validate.sh --firmware    # ESP-IDF 5.5.3 构建 + 合并 0x0 镜像
```

## 烧录

```bash
source ~/esp/esp-idf/export.sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/cardputer-mic-full.bin
```

预构建合并镜像在 [`firmware/`](firmware/) 目录。

## 验证

```bash
./tools/validate.sh               # 完整门禁（静态 + 固件）
```

真机验收已完成：
- BLE 配对 / 连接 / 配对码流程
- 10×30 秒连续听写（0 序列断档 / 0 CRC 错误）
- 3 轮断连恢复（最差 1.84 秒回到 READY）
- 三档拾音距离测试（1/5/15 cm，无削波）
- companion 菜单栏端到端听写上屏
- 库仑计数电量显示（NVS 持久化 + USB 充电校准）

## 已知限制

- **电量精度 ±10~15%**（库仑计数估算；本板 SAR ADC 硬件不可用于电压测量）
- 无 LE Audio / 原生蓝牙麦克风 Profile（ESP32-S3 在 ESP-IDF 5.5.3 不支持），
  需要 Mac 端 companion
- USB-C 仅供电 + 烧录（无 USB Audio Class）
- `G43`（I2S LRCK）与 UART0 TX 冲突，必须 `CONFIG_ESP_CONSOLE_NONE`——
  串口日志因设计关闭
