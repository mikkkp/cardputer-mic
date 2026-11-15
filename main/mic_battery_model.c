// main/mic_battery_model.c
// 库仑计数电量模型：NVS 持久化 + 三档电流消耗 + 充电回充 + 满电校准。
//
// NVS key: "mic" 命名空间 / "bat_mah" u32（剩余 mAh）。
// 写入策略：电量变化 ≥1% 或充电状态切换时落盘，避免 NVS 磨损。
#include "mic_battery_model.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "mic_bat";

// ---- 硬件参数（Cardputer ADV：1500mAh 锂聚）----
#define BAT_CAPACITY_MAH   1500
#define NVS_NAMESPACE      "mic"
#define NVS_KEY            "bat_mah"

// 电流估算（mA），来自 Cardputer ADV 社区实测 + ST7789 + NimBLE 典型值。
// 2026-11-15 P3-3/P4 观察补充：本项目运行时 CPU 80MHz + BLE 连接 + 屏幕常亮。
#define CUR_IDLE_MA        80    // 纯电池、屏幕 OFF、BLE 待机
#define CUR_SCREEN_MA      140   // 纯电池、屏幕 ON（+60mA 背光）
#define CUR_RECORD_MA      200   // 录音（+I2S DMA + ADPCM + BLE 突发）

// 充电回充（TP4057 默认 500mA，扣掉运行电流后净充 ~350mA；保守取 300）。
#define CUR_CHARGE_MA      300

// 判满时间：USB 连续在场超过此秒数 → 校准到满（TP4057 满充 STDBY 后电池
// 实际约 4.15~4.20V）。1.5h = 5400s。
#define CHG_HOLD_S         5400

// NVS 写入节流：电量变化超过此 mAh 才落盘（约 1%）。
#define NVS_FLUSH_MAH      15

static int  s_mah = -1;        // 剩余容量
static int  s_last_flush = -1; // 上次落盘时的 mAh
static bool s_chg_started = false;
static uint32_t s_chg_seconds = 0;
static bool s_full_calibrated = false;

static void nvs_flush(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    (void)nvs_set_u32(h, NVS_KEY, (uint32_t)s_mah);
    (void)nvs_commit(h);
    nvs_close(h);
    s_last_flush = s_mah;
}

void mic_battery_model_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t v = 0;
        if (nvs_get_u32(h, NVS_KEY, &v) == ESP_OK && v <= BAT_CAPACITY_MAH) {
            s_mah = (int)v;
        }
        nvs_close(h);
    }
    if (s_mah < 0) {
        s_mah = BAT_CAPACITY_MAH / 2;   // 首次：50% 保守起步
        ESP_LOGW(TAG, "NVS 无记录，按 50%% 起步");
    }
    s_last_flush = s_mah;
    ESP_LOGW(TAG, "电量模型就绪 %dmAh/%dmAh (%d%%)",
             s_mah, BAT_CAPACITY_MAH, mic_battery_model_soc());
}

void mic_battery_model_tick(bool usb_connected, bool recording, bool screen_on)
{
    static uint32_t s_ticks = 0;
    s_ticks++;

    if (usb_connected) {
        if (!s_chg_started) {
            s_chg_started = true;
            s_chg_seconds = 0;
            s_full_calibrated = false;
        }
        // 回充（mA * 1s / 3600 = mAh/s）
        s_mah += CUR_CHARGE_MA / 3600;   // 整数运算：300/3600 = 0 每秒 → 需要累计
        // 用 mAs 累计避免截断
        static uint32_t s_chg_mAs = 0;
        s_chg_mAs += CUR_CHARGE_MA;
        if (s_chg_mAs >= 3600) {
            s_mah += s_chg_mAs / 3600;
            s_chg_mAs %= 3600;
        }
        s_chg_seconds++;

        // 满电校准
        if (!s_full_calibrated && s_chg_seconds >= CHG_HOLD_S) {
            s_mah = BAT_CAPACITY_MAH;
            s_full_calibrated = true;
            nvs_flush();
            ESP_LOGW(TAG, "充电 %us，校准到满", (unsigned)s_chg_seconds);
        }
    } else {
        s_chg_started = false;
        s_chg_seconds = 0;
        s_full_calibrated = false;

        // 放电（三档电流取最大）
        int ma = CUR_IDLE_MA;
        if (screen_on) ma = CUR_SCREEN_MA;
        if (recording) ma = CUR_RECORD_MA;

        static uint32_t s_dis_mAs = 0;
        s_dis_mAs += (uint32_t)ma;
        if (s_dis_mAs >= 3600) {
            int d = (int)(s_dis_mAs / 3600);
            s_dis_mAs %= 3600;
            s_mah -= d;
            if (s_mah < 0) s_mah = 0;
        }
    }

    // 落盘节流
    if (s_last_flush < 0 ||
        (s_mah - s_last_flush >= NVS_FLUSH_MAH) ||
        (s_last_flush - s_mah >= NVS_FLUSH_MAH)) {
        nvs_flush();
    }
}

int mic_battery_model_soc(void)
{
    if (s_mah < 0) return -1;
    return s_mah * 100 / BAT_CAPACITY_MAH;
}

int mic_battery_model_soc_q8(void)
{
    if (s_mah < 0) return -1;
    return s_mah * 100 * 256 / BAT_CAPACITY_MAH;
}

int mic_battery_model_mah(void)
{
    return s_mah;
}

bool mic_battery_model_charge_full(void)
{
    return s_full_calibrated;
}
