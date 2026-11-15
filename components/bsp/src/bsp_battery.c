// components/bsp/src/bsp_battery.c
// 电池 ADC 采集（P2）：ADC1_CH9 @ GPIO10，2:1 分压。
// 7 次采样取中值抑制抖动；换算用 bsp_battery_soc 的纯函数（host 可测）。
#include "bsp_battery.h"
#include "bsp_battery_soc.h"
#include "bsp_pins.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "bsp_bat";

static adc_oneshot_unit_handle_t s_unit;
static adc_channel_t s_chan;
static adc_cali_handle_t s_cali;
static bool s_inited;

esp_err_t bsp_battery_init(void)
{
    if (s_inited) return ESP_OK;

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = (adc_unit_t)BSP_BAT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t e = adc_oneshot_new_unit(&unit_cfg, &s_unit);
    if (e != ESP_OK) { ESP_LOGE(TAG, "adc unit 失败: %s", esp_err_to_name(e)); return e; }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    e = adc_oneshot_config_channel(s_unit, (adc_channel_t)BSP_BAT_ADC_CHANNEL, &chan_cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "adc chan 失败: %s", esp_err_to_name(e)); return e; }
    s_chan = (adc_channel_t)BSP_BAT_ADC_CHANNEL;  // oneshot read 直接用通道号

    // 优先用曲线拟合校准（S3 出厂校准），退化为原始读数 × 线性估算。
    // 创建失败时 s_cali 必须置 NULL，否则会拿未初始化的句柄去换算。
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = (adc_unit_t)BSP_BAT_ADC_UNIT,
        .chan = (adc_channel_t)BSP_BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    s_cali = NULL;
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC 曲线校准不可用，用原始值估算");
        s_cali = NULL;
    }

    s_inited = true;
    ESP_LOGI(TAG, "电池 ADC 就绪 ADC%d_CH%d (GPIO%d) ratio=%d:%d cali=%s",
             BSP_BAT_ADC_UNIT, BSP_BAT_ADC_CHANNEL, BSP_BAT_ADC_GPIO,
             BSP_BAT_ADC_RATIO_NUM, BSP_BAT_ADC_RATIO_DEN,
             s_cali ? "curve" : "linear");
    return ESP_OK;
}

// 单次 ADC 原始读数 → 分压后引脚电压（mV）。失败返回 -1。
// 这里必须真的调用 adc_oneshot_read：曾因探针清理误删，导致 7 次采样全为 -1，
// 纯电池供电时电量永远无效。见 bsp_battery_soc.h 的回归测试说明。
static int sample_adc_mv(void)
{
    if (!s_inited || !s_unit) return -1;

    int raw[BSP_BAT_ADC_SAMPLES];
    for (int i = 0; i < BSP_BAT_ADC_SAMPLES; i++) {
        int v = 0;
        if (adc_oneshot_read(s_unit, s_chan, &v) != ESP_OK) return -1;
        raw[i] = v;
    }
    int mid = bsp_battery_median_raw(raw, BSP_BAT_ADC_SAMPLES);
    if (mid < 0) return -1;

    if (s_cali) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, mid, &mv) == ESP_OK) return mv;
        return -1;
    }
    return bsp_battery_raw_to_mv_fallback(mid);
}

int bsp_battery_mv(void)
{
    int mv = sample_adc_mv();
    if (mv < 0) return -1;
    return bsp_battery_mv_from_adc(mv, BSP_BAT_ADC_RATIO_NUM, BSP_BAT_ADC_RATIO_DEN);
}

int bsp_battery_soc(void)
{
    return bsp_battery_soc_from_mv(bsp_battery_mv());
}

int bsp_battery_soc_q8(void)
{
    int mv = sample_adc_mv();
    if (mv < 0) return -1;
    int cell = bsp_battery_mv_from_adc(mv, BSP_BAT_ADC_RATIO_NUM, BSP_BAT_ADC_RATIO_DEN);
    int pct = bsp_battery_soc_from_mv(cell);
    if (pct < 0) return -1;
    return pct * 256 / 100;
}

/* ---- 多档探针（v2）--------------------------------------------------------
   v1 教训：四档探针里每读一档就建/删一次校准方案，同衰减档两行 raw 差 600
   （同衰减不同换算，raw 本应完全一致）→ SAR ADC 模拟域被引用计数共享，
   建/删校准方案会拨弄它，后续读数全被带偏。
   v2 流程：unit 与两个校准方案一次性预创建，读数期间零触碰；每档配置后
   静置 600ms 再采样；序列 12dB → 6dB → 12dB 复读，第三行与第一行 raw 一致
   才可佐证读数可信。6dB 档满电时引脚 >1.75V 会顶到 4095，可作饱和探针。
   正式固件不调用；只在电池调查的诊断构建里用。 */
static int probe_read_median(adc_oneshot_unit_handle_t unit, adc_channel_t ch, int *out_mid)
{
    int v = 0;
    int raw[BSP_BAT_ADC_SAMPLES];
    for (int i = 0; i < BSP_BAT_ADC_SAMPLES; i++) {
        if (adc_oneshot_read(unit, ch, &v) != ESP_OK) return -1;
        raw[i] = v;
    }
    int mid = bsp_battery_median_raw(raw, BSP_BAT_ADC_SAMPLES);
    if (mid < 0) return -1;
    *out_mid = mid;
    return 0;
}

int bsp_battery_probe_all(bsp_bat_probe_t *out, int max)
{
    if (!out || max <= 0) return 0;

    adc_oneshot_unit_handle_t unit = NULL;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = (adc_unit_t)BSP_BAT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    if (adc_oneshot_new_unit(&unit_cfg, &unit) != ESP_OK) return 0;

    // 预创建两个校准方案；读数期间不再触碰模拟域。
    const adc_channel_t ch = (adc_channel_t)BSP_BAT_ADC_CHANNEL;
    adc_cali_handle_t cali12 = NULL, cali6 = NULL;
    adc_cali_curve_fitting_config_t c12 = {
        .unit_id = (adc_unit_t)BSP_BAT_ADC_UNIT, .chan = ch,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    (void)adc_cali_create_scheme_curve_fitting(&c12, &cali12);
    adc_cali_curve_fitting_config_t c6 = {
        .unit_id = (adc_unit_t)BSP_BAT_ADC_UNIT, .chan = ch,
        .atten = ADC_ATTEN_DB_6, .bitwidth = ADC_BITWIDTH_12,
    };
    (void)adc_cali_create_scheme_curve_fitting(&c6, &cali6);

    /* 读序列：12dB → 6dB → 12dB 复读。每档：配置通道 → 静置 → 采样。 */
    struct { adc_atten_t atten; int tag; int lin_full; } seq[] = {
        { ADC_ATTEN_DB_12, 12, 3100 },
        { ADC_ATTEN_DB_6,   6, 1750 },
        { ADC_ATTEN_DB_12, 12, 3100 },
    };
    int n = 0;
    for (int i = 0; i < (int)(sizeof(seq) / sizeof(seq[0])) && n < max; i++) {
        adc_oneshot_chan_cfg_t chan_cfg = { .bitwidth = ADC_BITWIDTH_12, .atten = seq[i].atten };
        if (adc_oneshot_config_channel(unit, ch, &chan_cfg) != ESP_OK) continue;

        vTaskDelay(pdMS_TO_TICKS(600));   // 分压网络 τ≈10ms，600ms 足够充分稳定

        int mid = -1;
        if (probe_read_median(unit, ch, &mid) != 0) continue;

        adc_cali_handle_t cali = (seq[i].tag == 12) ? cali12 : cali6;
        int mv = -1;
        if (cali) {
            if (adc_cali_raw_to_voltage(cali, mid, &mv) != ESP_OK) mv = -1;
        }

        out[n].raw = mid;
        out[n].mv = mv;
        out[n].atten = seq[i].tag;
        out[n].lin_full = seq[i].lin_full;
        n++;
    }

    // 全部读完后统一释放。
    if (cali12) (void)adc_cali_delete_scheme_curve_fitting(cali12);
    if (cali6)  (void)adc_cali_delete_scheme_curve_fitting(cali6);
    (void)adc_oneshot_del_unit(unit);
    return n;
}

/* ---- 单档顺序探针（v5）----------------------------------------------------
   v4 证实每次 read 都会损伤模拟域（S0→2515, S1→767, S2→683, S3→670）。
   v5 用 DIAG2 的可复现协议：init → 静置 300ms → 只读一组 → 永不再读。 */
int bsp_battery_probe_seq(int *raw_out, int *mv_out, int count)
{
    (void)count;  // v5 只读一组，count 参数保留兼容
    if (!raw_out || !mv_out) return 0;

    if (bsp_battery_init() != ESP_OK) return 0;   // 幂等：已初始化直接复用
    vTaskDelay(pdMS_TO_TICKS(300));               // DIAG2 可复现协议的静置

    int mid = -1;
    if (probe_read_median(s_unit, s_chan, &mid) != 0) {
        raw_out[0] = -1;
        mv_out[0] = -1;
        return 1;
    }

    int mv = -1;
    if (s_cali) {
        if (adc_cali_raw_to_voltage(s_cali, mid, &mv) != ESP_OK) mv = -1;
    }
    raw_out[0] = mid;
    mv_out[0] = mv;
    return 1;
}
