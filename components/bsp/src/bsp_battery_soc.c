// components/bsp/src/bsp_battery_soc.c
#include "bsp_battery_soc.h"
#include <stddef.h>
#include <stdlib.h>

// Li-ion 典型放电曲线的粗估锚点。这是电压估算，不是电量计精度。
// 单调、端点钳位，便于 host test 覆盖边界与单调性。
typedef struct {
    int mv;
    int soc;
} soc_point_t;

static const soc_point_t k_curve[] = {
    {3300, 0},
    {3500, 20},
    {3700, 45},
    {3900, 75},
    {4200, 100},
};

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int bsp_battery_median_raw(const int *raw, size_t n)
{
    if (!raw || n == 0) return -1;
    int buf[BSP_BAT_ADC_SAMPLES];
    if (n > BSP_BAT_ADC_SAMPLES) n = BSP_BAT_ADC_SAMPLES;
    for (size_t i = 0; i < n; i++) {
        // 无效读数不参与排序：全 -1 时返回 -1，避免把失败采样当成 0mV。
        if (raw[i] < 0) return -1;
        buf[i] = raw[i];
    }
    qsort(buf, n, sizeof(int), cmp_int);
    return buf[n / 2];
}

int bsp_battery_raw_to_mv_fallback(int raw)
{
    if (raw < 0) return -1;
    if (raw > 4095) raw = 4095;
    return raw * 3100 / 4095;
}

int bsp_battery_mv_from_adc(int adc_mv, uint32_t ratio_num, uint32_t ratio_den) {
    if (adc_mv < 0 || ratio_den == 0) return -1;
    // 四舍五入，避免整数除法在低电压段反复偏低。
    return (int)(((int64_t)adc_mv * ratio_num + ratio_den / 2) / ratio_den);
}

int bsp_battery_soc_from_mv(int cell_mv) {
    if (cell_mv <= 0) return -1;
    if (cell_mv <= k_curve[0].mv) return 0;

    const size_t n = sizeof(k_curve) / sizeof(k_curve[0]);
    for (size_t i = 1; i < n; i++) {
        if (cell_mv <= k_curve[i].mv) {
            int span_mv = k_curve[i].mv - k_curve[i - 1].mv;
            int span_soc = k_curve[i].soc - k_curve[i - 1].soc;
            int offset = cell_mv - k_curve[i - 1].mv;
            return k_curve[i - 1].soc + (offset * span_soc + span_mv / 2) / span_mv;
        }
    }
    return 100;
}
