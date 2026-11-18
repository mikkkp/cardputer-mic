# Cardputer Mic（Cardputer 蓝牙话筒）

[English](README.md) · **简体中文**

把 [Passport Mic](https://github.com/MatchlessHeroHx/ai-passport-mic) 蓝牙语音话筒
移植到 M5Stack Cardputer ADV 的固件（上游 MIT）。BLE 协议与 Mac 端 companion
零改动：`BLE → BlackHole 2ch → 豆包输入法`。

## 功能

按 **Ctrl** 键（左下角最底）对 Mac 上任意文本框听写。固件通过 BLE 以
16 kHz / 16-bit 单声道 IMA-ADPCM 流式传输音频，Mac 端 companion 送入豆包
输入法做语音转文字。

| 按键（丝印） | 功能 |
|---|---|
| **Ctrl**（最底） | 听写（开始 / 结束录音） |
| **Fn**（中间） | 回车 / 确认 |
| **Tab**（最上） | 撤销上次听写 |

电量百分比是软件估算值，显示在右上角。亮屏使用会扣减；熄屏静置 15 分钟后
视为在充电，累计约 2 小时后自动校准到 100%。整个过程不测量电压。

## 硬件

Stamp-S3A / ESP32-S3FN8，8 MB Flash，**无 PSRAM**，240×135 ST7789V2，
56 键 TCA8418，ES8311 + NS4150B，1500 mAh 电池。GPIO10 上的电池 SAR ADC
在这块板**不可用**（模拟域与 BT PHY、USB-JTAG 共享，init 后毫秒级失真），
因此电量是软件估算值（NVS 持久化 mAh 扣减 / 回补模型），不是测量值。

权威引脚表在 [`components/bsp/include/bsp_pins.h`](components/bsp/include/bsp_pins.h)，
由 `tests/test_bsp_pins.py` 检查。

关键值：内部 I2C `G8/G9`；ES8311 I2S `SCLK=G41`、`ASDOUT→MCU=G46`、
`LRCK=G43`、`DSDIN←MCU=G42`，**无独立 MCLK**（官方驱动 `MCLK=BCLK`）；
TCA8418 INT `G11`；`G38` 为 `DISP_BL` 兼 RGB LED `PWR_EN`。

## 构建

需要 ESP-IDF v5.5.3。

```bash
source ~/esp/esp-idf/export.sh
./tools/validate.sh --static      # host 测试，无需工具链
./tools/validate.sh --firmware    # 构建 + 合并 0x0 镜像
./tools/validate.sh               # 完整门禁（静态 + 固件）
```

## 烧录

```bash
source ~/esp/esp-idf/export.sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodemXXXXX -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/cardputer-mic-full.bin
```

预构建合并镜像在 [`firmware/`](firmware/) 目录，最新为
`cardputer-mic-v1.1.0.bin`。

## 目录结构

```text
components/bsp/    板级支持：引脚、I2C、音频、显示、键盘、按键映射
main/              应用层：状态机、UI、BLE、协议、电量模型
tests/             host 测试：按键映射、协议、UI 渲染、ADPCM、电量 SOC
tools/             验证门禁、字体生成、UI 预览、BLE 探针
firmware/          预构建合并镜像（v1.0.0、v1.1.0）
```

## 已知限制

- **电量是粗略估算，不是测量值**——本板 SAR ADC 硬件不可用于电压测量；充电
  由静置时长推断而非 USB 信号，因此没插充电器时长时间熄屏搁置也可能让显示值回升
- **USB 连接无法可靠检测**：`usb_serial_jtag_is_connected()` 监控的是主机 SOF 帧包，
  充电头不发 SOF，macOS 也可能在端口挂起时停发
- 无 LE Audio / 原生蓝牙麦克风 Profile（ESP32-S3 在 ESP-IDF 5.5.3 不支持），
  必须配合 Mac 端 companion
- USB-C 仅供电 + 烧录（无 USB Audio Class）
- `G43`（I2S LRCK）与 UART0 TX 冲突，必须 `CONFIG_ESP_CONSOLE_NONE`——
  串口日志因设计关闭

## 致谢

上游固件与协议来自
[MatchlessHeroHx/ai-passport-mic](https://github.com/MatchlessHeroHx/ai-passport-mic)
与 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport)，均为 MIT。
详见 [NOTICE](NOTICE)。
