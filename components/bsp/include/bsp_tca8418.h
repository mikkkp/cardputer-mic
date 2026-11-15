// components/bsp/include/bsp_tca8418.h
// TCA8418 寄存器地址与初始化序列（纯数据，无 IDF 依赖，可被 host test 校验）。
//
// 事实来源（本地官方 UserDemo，MIT）：
//   ../userdemo-adv/main/hal/utils/adafruit_tca8418/Adafruit_TCA8418_registers.h
//   ../userdemo-adv/main/hal/utils/adafruit_tca8418/Adafruit_TCA8418.cpp  (begin / matrix / flush /
//                                                                          enableInterrupts / getEvent)
//   ../userdemo-adv/main/hal/keyboard/keybaord.cpp                          (Keyboard::init / update)
#pragma once

#include <stddef.h>
#include <stdint.h>

// ---- 寄存器地址 ----
#define BSP_TCA8418_REG_CFG             0x01
#define BSP_TCA8418_REG_INT_STAT        0x02
#define BSP_TCA8418_REG_KEY_LCK_EC      0x03
#define BSP_TCA8418_REG_KEY_EVENT_A     0x04
#define BSP_TCA8418_REG_KEY_EVENT_B     0x05
#define BSP_TCA8418_REG_KEY_EVENT_C     0x06
#define BSP_TCA8418_REG_KEY_EVENT_D     0x07
#define BSP_TCA8418_REG_KEY_EVENT_E     0x08
#define BSP_TCA8418_REG_KEY_EVENT_F     0x09
#define BSP_TCA8418_REG_KEY_EVENT_G     0x0A
#define BSP_TCA8418_REG_KEY_EVENT_H     0x0B
#define BSP_TCA8418_REG_KEY_EVENT_I     0x0C
#define BSP_TCA8418_REG_KEY_EVENT_J     0x0D
#define BSP_TCA8418_REG_KP_LCK_TIMER    0x0E
#define BSP_TCA8418_REG_UNLOCK_1        0x0F
#define BSP_TCA8418_REG_UNLOCK_2        0x10
#define BSP_TCA8418_REG_GPIO_INT_STAT_1 0x11
#define BSP_TCA8418_REG_GPIO_INT_STAT_2 0x12
#define BSP_TCA8418_REG_GPIO_INT_STAT_3 0x13
#define BSP_TCA8418_REG_GPIO_DAT_STAT_1 0x14
#define BSP_TCA8418_REG_GPIO_DAT_STAT_2 0x15
#define BSP_TCA8418_REG_GPIO_DAT_STAT_3 0x16
#define BSP_TCA8418_REG_GPIO_DAT_OUT_1  0x17
#define BSP_TCA8418_REG_GPIO_DAT_OUT_2  0x18
#define BSP_TCA8418_REG_GPIO_DAT_OUT_3  0x19
#define BSP_TCA8418_REG_GPIO_INT_EN_1   0x1A
#define BSP_TCA8418_REG_GPIO_INT_EN_2   0x1B
#define BSP_TCA8418_REG_GPIO_INT_EN_3   0x1C
#define BSP_TCA8418_REG_KP_GPIO_1       0x1D
#define BSP_TCA8418_REG_KP_GPIO_2       0x1E
#define BSP_TCA8418_REG_KP_GPIO_3       0x1F
#define BSP_TCA8418_REG_GPI_EM_1        0x20
#define BSP_TCA8418_REG_GPI_EM_2        0x21
#define BSP_TCA8418_REG_GPI_EM_3        0x22
#define BSP_TCA8418_REG_GPIO_DIR_1      0x23
#define BSP_TCA8418_REG_GPIO_DIR_2      0x24
#define BSP_TCA8418_REG_GPIO_DIR_3      0x25
#define BSP_TCA8418_REG_GPIO_INT_LVL_1  0x26
#define BSP_TCA8418_REG_GPIO_INT_LVL_2  0x27
#define BSP_TCA8418_REG_GPIO_INT_LVL_3  0x28
#define BSP_TCA8418_REG_DEBOUNCE_DIS_1  0x29
#define BSP_TCA8418_REG_DEBOUNCE_DIS_2  0x2A
#define BSP_TCA8418_REG_DEBOUNCE_DIS_3  0x2B
#define BSP_TCA8418_REG_GPIO_PULL_1     0x2C
#define BSP_TCA8418_REG_GPIO_PULL_2     0x2D
#define BSP_TCA8418_REG_GPIO_PULL_3     0x2E

// ---- 字段 ----
#define BSP_TCA8418_CFG_AI              0x80
#define BSP_TCA8418_CFG_GPI_IEN         0x02
#define BSP_TCA8418_CFG_KE_IEN          0x01
#define BSP_TCA8418_CFG_IRQ_MASK        0x03  // 官方 enableInterrupts(): GPI_IEN | KE_IEN
#define BSP_TCA8418_STAT_K_INT          0x01
#define BSP_TCA8418_STAT_GPI_INT        0x02
#define BSP_TCA8418_FLUSH_INT_STAT      0x03  // 官方 flush(): 写 INT_STAT = K_INT | GPI_INT

// ---- 初始化固定写入序列 ----
// 顺序照官方 Keyboard::init()：begin() -> matrix(7,8) -> flush()。
// CFG 的中断使能是「读改写」，不在此表内，由 bsp_tca8418_cfg_enable_interrupts() 计算。
typedef struct {
    uint8_t reg;
    uint8_t value;
} bsp_tca8418_write_t;

extern const bsp_tca8418_write_t bsp_tca8418_init_writes[];
extern const size_t bsp_tca8418_init_writes_count;

// 只置位 GPI_IEN / KE_IEN，保留 CFG 其余位（如 AI）；可重复调用。
uint8_t bsp_tca8418_cfg_enable_interrupts(uint8_t current_cfg);
