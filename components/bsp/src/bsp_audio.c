// components/bsp/src/bsp_audio.c
// ES8311 录音 bring-up（P1b）。
//
// 设计决策：
//  - 不引入 esp_codec_dev 组件：P1 只验证「I2C 配置 + I2S 采集」链路；
//    寄存器序列逐条来自 M5Unified Cardputer ADV（本地一手来源）。
//  - 本板无独立 MCLK：ES8311 REG01=0xBA 表示「MCLK 由 BCLK 驱动」，
//    REG02=0x18 = MULT_PRE... 在 BCLK=512kHz（16kHz×32bit slot）下内部 256fs。
//  - I2S 用 std 全双工：RX 为主（录音），TX 同建但只写 0（静音，保持 ES8311
//    从模式所需时钟；本板扬声器经 ES8311 DAC，首版不播音）。
//  - 槽位：std 立体声 32bit slot（BCLK=16k*64=1.024MHz 或 16bit slot 512kHz）。
//    ES8311 ASDOUT 在 WS 低半帧（左槽）出数据；mono 采集取左槽。
#include "bsp_audio.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_audio";

// read_mono 单次内部抽取块大小：640 样本对（1.28KBx2 交织）足以覆盖 20ms 帧，
// 又不会往静态区放太大缓冲。
#define MIC_MONO_CHUNK_SAMPLES 640

static i2s_chan_handle_t s_rx;
static i2s_chan_handle_t s_tx;
static i2c_master_dev_handle_t s_dev;
static bool s_inited;

// ---- ES8311 寄存器读写 ------------------------------------------------------
static esp_err_t es8311_write(uint8_t reg, uint8_t val)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

esp_err_t bsp_audio_reg_read(uint8_t reg, uint8_t *value)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, 50);
}

// 初始化序列：M5Unified _microphone_enabled_cb_cardputer_adv 的 enabled 序列。
// ADC 音量 0x17 用 0xBF（±0dB，M5 ADV 实测值），不用 atomic_echo 的 0xFF。
static const struct { uint8_t reg, val; } s_mic_init[] = {
    { 0x00, 0x80 },  // CSM power on
    { 0x01, 0xBA },  // MCLK=BCLK（本板无独立 MCLK）
    { 0x02, 0x18 },  // MULT_PRE：BCLK 内部分频得 256fs
    { 0x0D, 0x01 },  // 模拟部分上电
    { 0x0E, 0x02 },  // enable analog PGA + ADC modulator
    { 0x14, 0x1A },  // 差分 Mic1p-Mic1n + PGA 基准档（同 esp_codec_dev es8311_start）
    { 0x16, 0x04 },  // MIC PGA 24dB（7 档：0/6/12/18/24/30/36/42dB，可调）
    { 0x17, 0xBF },  // ADC volume ±0 dB
    { 0x1C, 0x6A },  // ADC EQ bypass + 数字域 DC 偏置消除
};

esp_err_t bsp_audio_init(void)
{
    if (s_inited) return ESP_OK;

    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) return e;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_I2C_ES8311_ADDR,
        .scl_speed_hz    = BSP_I2C_CLK_HZ,
    };
    e = i2c_master_bus_add_device(bsp_i2c_bus(), &dev_cfg, &s_dev);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 add device failed: %s", esp_err_to_name(e));
        s_dev = NULL;
        return e;
    }

    // ES8311 初始化序列 + 回读校验
    for (size_t i = 0; i < sizeof(s_mic_init) / sizeof(s_mic_init[0]); i++) {
        e = es8311_write(s_mic_init[i].reg, s_mic_init[i].val);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "ES8311 write REG%02X failed: %s",
                     s_mic_init[i].reg, esp_err_to_name(e));
            return e;
        }
    }
    // 回读两个关键寄存器确认 I2C 链路与寄存器语义正确
    uint8_t v01 = 0, v0e = 0;
    bsp_audio_reg_read(0x01, &v01);
    bsp_audio_reg_read(0x0E, &v0e);
    ESP_LOGI(TAG, "ES8311 ready: REG01=0x%02X (want BA) REG0E=0x%02X (want 02)", v01, v0e);

    // ---- I2S 全双工（RX 录音主用）------------------------------------------
    i2s_chan_config_t chan = {
        .id = (i2s_port_t)BSP_I2S_PORT,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 6,
        .dma_frame_num = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    e = i2s_new_channel(&chan, &s_tx, &s_rx);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(e));
        return e;
    }

    // 16bit 槽立体声：BCLK = 16000 × 2ch × 16bit = 512 kHz。
    // 与 passport-mic 实测配置一致（16bit slot + left_align=true）：
    // ES8311 ASDOUT 在 WS 低半帧（左槽）出有效样本，右槽是哑数据；
    // M5 序列 REG02=0x18 在 512kHz BCLK 下内部分频恰得 256fs。
    // left_align=true：样本在槽内左对齐（MSB first），与 16bit 数据宽度一致。
    i2s_std_config_t std = {
        .clk_cfg = {
            .sample_rate_hz = 16000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)BSP_I2S_BCLK,
            .ws = (gpio_num_t)BSP_I2S_WS,
            .dout = (gpio_num_t)BSP_I2S_DOUT,
            .din = (gpio_num_t)BSP_I2S_DIN,
            .invert_flags = { false, false, false },
        },
    };
    e = i2s_channel_init_std_mode(s_tx, &std);
    if (e == ESP_OK) e = i2s_channel_init_std_mode(s_rx, &std);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s init failed: %s", esp_err_to_name(e));
        return e;
    }
    // TX 通道写静音占位（保持 BCLK/WS 输出；RX 为数据接收方）
    static int16_t s_silence[240];  // 静态零缓冲
    e = i2s_channel_enable(s_tx);
    size_t written = 0;
    if (e == ESP_OK) e = i2s_channel_write(s_tx, s_silence, sizeof(s_silence), &written, 100 / portTICK_PERIOD_MS);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx start failed: %s", esp_err_to_name(e));
        return e;
    }
    e = i2s_channel_enable(s_rx);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s rx enable failed: %s", esp_err_to_name(e));
        return e;
    }

    s_inited = true;
    ESP_LOGI(TAG, "I2S ready: 16kHz/16bit capture (16bit slot stereo, L=mic)");
    return ESP_OK;
}

esp_err_t bsp_audio_read(void *pcm, size_t bytes)
{
    if (!s_inited || !s_rx) return ESP_ERR_INVALID_STATE;
    size_t got = 0;
    esp_err_t e = i2s_channel_read(s_rx, pcm, bytes, &got, portMAX_DELAY);
    if (e != ESP_OK) return e;
    if (got != bytes) return ESP_ERR_INVALID_SIZE;
    return ESP_OK;
}

// 交错 L/R → 抽 L 槽到 out。静态缓冲避免每次栈上大数组。
// 一次抽满 samples 个：循环读直到凑够（i2s_channel_read 内部按 DMA 块返回）。
esp_err_t bsp_audio_read_mono(void *out, size_t samples)
{
    if (!s_inited || !s_rx) return ESP_ERR_INVALID_STATE;
    static int16_t s_interleaved[MIC_MONO_CHUNK_SAMPLES * 2];
    int16_t *dst = (int16_t *)out;
    size_t done = 0;
    while (done < samples) {
        size_t want_samples = samples - done;
        if (want_samples > MIC_MONO_CHUNK_SAMPLES) want_samples = MIC_MONO_CHUNK_SAMPLES;
        size_t want_bytes = want_samples * 2 * sizeof(int16_t);  // L+R
        size_t got = 0;
        esp_err_t e = i2s_channel_read(s_rx, s_interleaved, want_bytes, &got, portMAX_DELAY);
        if (e != ESP_OK) return e;
        size_t got_samples = got / (2 * sizeof(int16_t));
        for (size_t i = 0; i < got_samples; i++) {
            dst[done + i] = s_interleaved[i * 2];  // L 槽 = 麦克风
        }
        done += got_samples;
        if (got_samples == 0 && got == 0) {
            // 没数据且超时；避免死循环
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}
