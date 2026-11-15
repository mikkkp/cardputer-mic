// tests/test_bsp_tca8418.c
// 固化官方 UserDemo 的 TCA8418 寄存器地址与初始化序列，防止移植时凭记忆写错。
#include "bsp_tca8418.h"
#include <assert.h>
#include <stdio.h>

static void test_register_addresses(void) {
    assert(BSP_TCA8418_REG_CFG == 0x01);
    assert(BSP_TCA8418_REG_INT_STAT == 0x02);
    assert(BSP_TCA8418_REG_KEY_EVENT_A == 0x04);
    assert(BSP_TCA8418_REG_GPIO_INT_STAT_1 == 0x11);
    assert(BSP_TCA8418_REG_GPIO_INT_EN_1 == 0x1A);
    assert(BSP_TCA8418_REG_KP_GPIO_1 == 0x1D);
    assert(BSP_TCA8418_REG_KP_GPIO_2 == 0x1E);
    assert(BSP_TCA8418_REG_KP_GPIO_3 == 0x1F);
    assert(BSP_TCA8418_REG_GPI_EM_1 == 0x20);
    assert(BSP_TCA8418_REG_GPIO_DIR_1 == 0x23);
    assert(BSP_TCA8418_REG_GPIO_INT_LVL_1 == 0x26);
    assert(BSP_TCA8418_REG_GPIO_PULL_3 == 0x2E);
}

static void test_init_writes_match_official_sequence(void) {
    static const bsp_tca8418_write_t expected[] = {
        {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00},
        {0x20, 0xFF}, {0x21, 0xFF}, {0x22, 0xFF},
        {0x26, 0x00}, {0x27, 0x00}, {0x28, 0x00},
        {0x1A, 0xFF}, {0x1B, 0xFF}, {0x1C, 0xFF},
        {0x1D, 0x7F}, {0x1E, 0xFF},
        {0x02, 0x03},
    };
    assert(bsp_tca8418_init_writes_count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < bsp_tca8418_init_writes_count; i++) {
        assert(bsp_tca8418_init_writes[i].reg == expected[i].reg);
        assert(bsp_tca8418_init_writes[i].value == expected[i].value);
    }
}

static void test_cfg_read_modify_write(void) {
    // 官方 enableInterrupts(): read CFG, |= (GPI_IEN | KE_IEN), write back.
    assert(BSP_TCA8418_CFG_IRQ_MASK == 0x03);
    assert(bsp_tca8418_cfg_enable_interrupts(0x00) == 0x03);
    assert(bsp_tca8418_cfg_enable_interrupts(0x80) == 0x83);  // 保留 AI 位
    assert(bsp_tca8418_cfg_enable_interrupts(0x03) == 0x03);  // 幂等
    assert(bsp_tca8418_cfg_enable_interrupts(0xFF) == 0xFF);
    assert(BSP_TCA8418_FLUSH_INT_STAT ==
           (BSP_TCA8418_STAT_K_INT | BSP_TCA8418_STAT_GPI_INT));
}

int main(void) {
    test_register_addresses();
    test_init_writes_match_official_sequence();
    test_cfg_read_modify_write();
    puts("test_bsp_tca8418: PASS");
    return 0;
}
