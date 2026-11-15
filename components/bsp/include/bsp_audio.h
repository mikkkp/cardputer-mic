// components/bsp/include/bsp_audio.h
// ES8311 + I2S 录音（P1b bring-up 版）。
// 本板无独立 MCLK，ES8311 配置为 MCLK=BCLK（REG01=0xBA 语义），
// 寄存器序列来自 M5Unified Cardputer ADV 的 _microphone_enabled_cb。
#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

// 初始化 ES8311（I2C 配置寄存器）与 I2S0 全双工通道，按 16kHz/16bit/mono 打开。
// 内部会调 bsp_i2c_init()（幂等）。成功可重复调用（幂等）。
esp_err_t bsp_audio_init(void);

// 读一块 PCM。bytes 为字节数；16bit mono 时 = 采样数 x 2。
// 阻塞直到读满或出错。返回 ESP_OK 或 I2S 错误码。（原始交错 L/R 槽）
esp_err_t bsp_audio_read(void *pcm, size_t bytes);

// 读 mono PCM：内部读交错 L/R 后抽取 L 槽（麦克风数据）到 out。
// samples 为期望的采样数（16bit）；out 容量 ≥ samples*2 字节。
// 阻塞直到读满。返回 ESP_OK 或错误。
esp_err_t bsp_audio_read_mono(void *out, size_t samples);

// 供诊断用：读取 ES8311 寄存器。
esp_err_t bsp_audio_reg_read(uint8_t reg, uint8_t *value);
