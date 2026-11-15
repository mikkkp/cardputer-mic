// components/bsp/src/bsp_keyboard.c
// TCA8418 硬件层：I2C 初始化序列、G11 中断、FIFO 读取。
// 事实来源见 bsp_tca8418.h 与 ../userdemo-adv/main/hal/keyboard/keybaord.cpp。
#include "bsp_keyboard.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "bsp_tca8418.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "bsp_kbd";

static i2c_master_dev_handle_t s_dev;
static bsp_keyboard_event_cb_t s_cb;
static volatile bool s_isr_flag;

static void IRAM_ATTR keyboard_isr(void *arg) {
    (void)arg;
    s_isr_flag = true;
}

esp_err_t bsp_keyboard_reg_write(uint8_t reg, uint8_t value) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

esp_err_t bsp_keyboard_reg_read(uint8_t reg, uint8_t *value) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, value, 1, 50);
}

esp_err_t bsp_keyboard_init(void) {
    if (s_dev) return ESP_OK;  // 幂等

    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) return e;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_I2C_TCA8418_ADDR,
        .scl_speed_hz = BSP_I2C_CLK_HZ,
    };
    e = i2c_master_bus_add_device(bsp_i2c_bus(), &dev_cfg, &s_dev);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "TCA8418 添加设备失败: %s（0x%02X 是否应答？）",
                 esp_err_to_name(e), BSP_I2C_TCA8418_ADDR);
        s_dev = NULL;
        return e;
    }

    for (size_t i = 0; i < bsp_tca8418_init_writes_count; i++) {
        const bsp_tca8418_write_t *w = &bsp_tca8418_init_writes[i];
        e = bsp_keyboard_reg_write(w->reg, w->value);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "初始化写 REG0x%02X=0x%02X 失败: %s",
                     w->reg, w->value, esp_err_to_name(e));
            return e;
        }
    }

    // CFG 的中断使能是读改写，不覆盖 AI 等其余位。
    uint8_t cfg = 0;
    e = bsp_keyboard_reg_read(BSP_TCA8418_REG_CFG, &cfg);
    if (e != ESP_OK) return e;
    const uint8_t new_cfg = bsp_tca8418_cfg_enable_interrupts(cfg);
    e = bsp_keyboard_reg_write(BSP_TCA8418_REG_CFG, new_cfg);
    if (e != ESP_OK) return e;
    ESP_LOGI(TAG, "TCA8418 就绪：CFG 0x%02X -> 0x%02X，INT=GPIO%d",
             cfg, new_cfg, BSP_KEY_INT_GPIO);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_KEY_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,  // TCA8418 低有效，但边沿都监听更稳
    };
    e = gpio_config(&io);
    if (e != ESP_OK) return e;

    esp_err_t isr_e = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (isr_e != ESP_OK && isr_e != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "gpio_install_isr_service: %s", esp_err_to_name(isr_e));
    }
    e = gpio_isr_handler_add((gpio_num_t)BSP_KEY_INT_GPIO, keyboard_isr, NULL);
    if (e != ESP_OK) return e;

    return ESP_OK;
}

void bsp_keyboard_set_event_cb(bsp_keyboard_event_cb_t cb) { s_cb = cb; }

int bsp_keyboard_poll(void) {
    if (!s_dev || !s_isr_flag) return 0;

    int handled = 0;
    uint8_t ev = 0;
    // FIFO 最多 10 个事件；多读几次防止残留。
    for (int i = 0; i < 16; i++) {
        if (bsp_keyboard_reg_read(BSP_TCA8418_REG_KEY_EVENT_A, &ev) != ESP_OK) break;
        if (ev == 0) break;  // FIFO 空

        bsp_key_raw_t raw;
        if (!bsp_keymap_decode(ev, &raw)) {
            ESP_LOGW(TAG, "无法解码 FIFO 字节 0x%02X", ev);
            continue;
        }
        bsp_key_t key;
        bsp_keymap_remap(&raw, &key);
        const bsp_btn_t btn = bsp_keymap_button(&key);
        handled++;
        if (s_cb) s_cb(&raw, &key, btn);
    }

    // 清 K_INT；若清完仍有挂起位，保留 isr 标志供下一轮继续。
    uint8_t stat = 0;
    if (bsp_keyboard_reg_read(BSP_TCA8418_REG_INT_STAT, &stat) == ESP_OK &&
        (stat & BSP_TCA8418_STAT_K_INT)) {
        (void)bsp_keyboard_reg_write(BSP_TCA8418_REG_INT_STAT, BSP_TCA8418_STAT_K_INT);
        (void)bsp_keyboard_reg_read(BSP_TCA8418_REG_INT_STAT, &stat);
    }
    if (!(stat & BSP_TCA8418_STAT_K_INT)) {
        s_isr_flag = false;
    }
    return handled;
}
