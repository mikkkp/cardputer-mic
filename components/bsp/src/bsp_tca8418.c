// components/bsp/src/bsp_tca8418.c
// 见 bsp_tca8418.h 的事实来源。此文件只含纯数据，方便 host test 逐条比对官方实现。
#include "bsp_tca8418.h"

const bsp_tca8418_write_t bsp_tca8418_init_writes[] = {
    // Adafruit_TCA8418::begin(): 所有 GIO 输入 / 加入按键事件 / 下降沿 / 开中断
    {BSP_TCA8418_REG_GPIO_DIR_1,     0x00},
    {BSP_TCA8418_REG_GPIO_DIR_2,     0x00},
    {BSP_TCA8418_REG_GPIO_DIR_3,     0x00},
    {BSP_TCA8418_REG_GPI_EM_1,       0xFF},
    {BSP_TCA8418_REG_GPI_EM_2,       0xFF},
    {BSP_TCA8418_REG_GPI_EM_3,       0xFF},
    {BSP_TCA8418_REG_GPIO_INT_LVL_1, 0x00},
    {BSP_TCA8418_REG_GPIO_INT_LVL_2, 0x00},
    {BSP_TCA8418_REG_GPIO_INT_LVL_3, 0x00},
    {BSP_TCA8418_REG_GPIO_INT_EN_1,  0xFF},
    {BSP_TCA8418_REG_GPIO_INT_EN_2,  0xFF},
    {BSP_TCA8418_REG_GPIO_INT_EN_3,  0xFF},
    // matrix(7, 8): 7 行 + 8 列，KP_GPIO_3 不写（列数未超过 8）
    {BSP_TCA8418_REG_KP_GPIO_1,      0x7F},
    {BSP_TCA8418_REG_KP_GPIO_2,      0xFF},
    // flush(): 清 INT_STAT 的按键与 GPI 中断标志
    {BSP_TCA8418_REG_INT_STAT,       BSP_TCA8418_FLUSH_INT_STAT},
};

const size_t bsp_tca8418_init_writes_count =
    sizeof(bsp_tca8418_init_writes) / sizeof(bsp_tca8418_init_writes[0]);

uint8_t bsp_tca8418_cfg_enable_interrupts(uint8_t current_cfg)
{
    return (uint8_t)(current_cfg | BSP_TCA8418_CFG_IRQ_MASK);
}
