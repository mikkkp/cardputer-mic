// tests/test_bsp_battery_soc.c
// 电池 ADC → 电压 → SOC 的边界、单调性与采样中值测试。
#include "bsp_battery_soc.h"
#include <assert.h>
#include <stdio.h>

static void test_mv_from_adc(void) {
    assert(bsp_battery_mv_from_adc(2100, 2, 1) == 4200);
    assert(bsp_battery_mv_from_adc(1850, 2, 1) == 3700);
    assert(bsp_battery_mv_from_adc(0, 2, 1) == 0);
    assert(bsp_battery_mv_from_adc(1000, 1, 1) == 1000);
    assert(bsp_battery_mv_from_adc(-1, 2, 1) == -1);
    assert(bsp_battery_mv_from_adc(1000, 2, 0) == -1);
}

static void test_soc_boundaries(void) {
    assert(bsp_battery_soc_from_mv(-1) == -1);
    assert(bsp_battery_soc_from_mv(0) == -1);
    assert(bsp_battery_soc_from_mv(3300) == 0);
    assert(bsp_battery_soc_from_mv(4200) == 100);
    assert(bsp_battery_soc_from_mv(4250) == 100);  // 上钳位
    assert(bsp_battery_soc_from_mv(3200) == 0);    // 下钳位
    assert(bsp_battery_soc_from_mv(3700) == 45);   // 锚点精确命中
}

static void test_soc_is_monotonic(void) {
    int prev = -1;
    for (int mv = 3000; mv <= 4400; mv += 10) {
        int soc = bsp_battery_soc_from_mv(mv);
        assert(soc >= 0 && soc <= 100);
        assert(soc >= prev);
        prev = soc;
    }
}

// 回归：ADC 驱动调用曾被误删，7 次采样全填 -1 却依然能编译通过，
// 导致纯电池供电时电量永远无效。中值函数必须拒绝无效读数、保留真实采样。
static void test_median_raw(void) {
    const int typical[BSP_BAT_ADC_SAMPLES] = {2048, 2051, 2049, 2047, 2050, 2048, 2052};
    assert(bsp_battery_median_raw(typical, BSP_BAT_ADC_SAMPLES) == 2049);

    const int with_noise[BSP_BAT_ADC_SAMPLES] = {2048, 3000, 2049, 10, 2050, 2048, 2052};
    assert(bsp_battery_median_raw(with_noise, BSP_BAT_ADC_SAMPLES) == 2049);

    const int all_invalid[BSP_BAT_ADC_SAMPLES] = {-1, -1, -1, -1, -1, -1, -1};
    assert(bsp_battery_median_raw(all_invalid, BSP_BAT_ADC_SAMPLES) == -1);
    assert(bsp_battery_median_raw(NULL, BSP_BAT_ADC_SAMPLES) == -1);
    assert(bsp_battery_median_raw(typical, 0) == -1);
}

static void test_raw_to_mv_fallback(void) {
    assert(bsp_battery_raw_to_mv_fallback(-1) == -1);
    assert(bsp_battery_raw_to_mv_fallback(0) == 0);
    assert(bsp_battery_raw_to_mv_fallback(4095) == 3100);
    assert(bsp_battery_raw_to_mv_fallback(4096) == 3100);  // 钳位
    // 典型满电：分压后约 2050mV → 电芯约 4100mV → SOC 92%
    int pin = bsp_battery_raw_to_mv_fallback(2708);
    int cell = bsp_battery_mv_from_adc(pin, 2, 1);
    assert(cell > 4000 && cell < 4200);
    assert(bsp_battery_soc_from_mv(cell) > 85);
}

int main(void) {
    test_mv_from_adc();
    test_soc_boundaries();
    test_soc_is_monotonic();
    test_median_raw();
    test_raw_to_mv_fallback();
    puts("test_bsp_battery_soc: PASS");
    return 0;
}
