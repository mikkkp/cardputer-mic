// components/bsp/src/bsp_display.c
// ST7789V2 240x135 横屏最小 bring-up（esp_lcd，无 LVGL）。
// 参数来源：../userdemo-adv/components/M5GFX/src/M5GFX.cpp 的 CardputerADV 分支
//   panel_width=135, panel_height=240, offset_x=52, offset_y=40, invert=true,
//   rotation=1（90° -> 240x135 横屏）, pin_cs=37, pin_rst=33, spi_host=SPI3, 40MHz。
// 注意 bsp_pins.h 的 BSP_LCD_SPI_HOST=2 在 spi_host_device_t 里就是 SPI3_HOST。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

static const char *TAG = "bsp_lcd";

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_ready;

static void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t color) {    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= BSP_LCD_H) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= BSP_LCD_W) continue;
            fb[yy * BSP_LCD_W + xx] = color;
        }
    }
}

void bsp_display_backlight(bool on) {
    static bool configured;
    if (!configured) {
        gpio_config_t bl = {
            .pin_bit_mask = 1ULL << BSP_LCD_BL,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t e = gpio_config(&bl);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "背光 GPIO%d 配置失败: %s", BSP_LCD_BL, esp_err_to_name(e));
            return;
        }
        configured = true;
    }
    (void)gpio_set_level((gpio_num_t)BSP_LCD_BL, on ? 1 : 0);
}

esp_err_t bsp_display_init(void) {
    if (s_ready) return ESP_OK;

    // 背光先关，避免上电花屏。
    bsp_display_backlight(false);

    spi_bus_config_t buscfg = {
        .mosi_io_num = BSP_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = BSP_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LCD_W * BSP_LCD_H * sizeof(uint16_t),
    };
    esp_err_t e = spi_bus_initialize((spi_host_device_t)BSP_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI 总线初始化失败: %s", esp_err_to_name(e));
        return e;
    }

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BSP_LCD_CS,
        .dc_gpio_num = BSP_LCD_DC,
        .spi_mode = BSP_LCD_SPI_MODE,
        .pclk_hz = BSP_LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    e = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_cfg, &s_io);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel io 创建失败: %s", esp_err_to_name(e));
        return e;
    }

    esp_lcd_panel_dev_config_t pcfg = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    e = esp_lcd_new_panel_st7789(s_io, &pcfg, &s_panel);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 面板创建失败: %s", esp_err_to_name(e));
        return e;
    }

    e = esp_lcd_panel_reset(s_panel);
    if (e != ESP_OK) return e;
    e = esp_lcd_panel_init(s_panel);
    if (e != ESP_OK) return e;

    // esp_lcd 的 swap_xy 只设 MADCTL.MV、不会交换 gap；而 lgfx 在 rotation=1 会把
    // offset 由 (52,40) 交换成 (40,52)。这里手动交换 gap：swap 后逻辑宽 240 映射到
    // RASET(行)、逻辑高 135 映射到 CASET(列)，才使 135x240 面板居中于 240x320 GRAM。
    e = esp_lcd_panel_invert_color(s_panel, true);
    if (e != ESP_OK) return e;
    e = esp_lcd_panel_swap_xy(s_panel, true);          // rotation 1
    if (e != ESP_OK) return e;
    e = esp_lcd_panel_mirror(s_panel, true, false);    // MADCTL 0x60 = MV|MX
    if (e != ESP_OK) return e;
    // lgfx Panel_LCD::set_window 与 esp_lcd 同构：CASET=逻辑x+_colstart、RASET=逻辑y+_rowstart。
    // Cardputer ADV rotation=1 精确解：_colstart=40（行窗口 40..279 填满面板 240 行），
    // _rowstart=53（列窗口 53..187 填满面板 135 列）。y_gap 差 1 会在底边留 1px 马赛克线。
    e = esp_lcd_panel_set_gap(s_panel, 40, 53);
    if (e != ESP_OK) return e;
    e = esp_lcd_panel_disp_on_off(s_panel, true);
    if (e != ESP_OK) return e;

    bsp_display_backlight(true);
    s_ready = true;
    ESP_LOGI(TAG, "ST7789 就绪 %dx%d invert=1 gap=(40,53) swap_xy=1 mirror=(1,0) BL=GPIO%d",
             BSP_LCD_W, BSP_LCD_H, BSP_LCD_BL);
    return ESP_OK;
}

void bsp_display_draw_test(void) {
    if (!s_panel) return;
    const size_t pixels = (size_t)BSP_LCD_W * BSP_LCD_H;
    uint16_t *fb = heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!fb) {
        ESP_LOGE(TAG, "帧缓冲分配失败（%u 字节）", (unsigned)(pixels * 2));
        return;
    }
    for (size_t i = 0; i < pixels; i++) fb[i] = 0x0000;  // 黑底

    const int m = 20;
    fill_rect(fb, 0, 0, m, m, 0xF800);                          // 左上 红
    fill_rect(fb, BSP_LCD_W - m, 0, m, m, 0x07E0);              // 右上 绿
    fill_rect(fb, 0, BSP_LCD_H - m, m, m, 0x001F);              // 左下 蓝
    fill_rect(fb, BSP_LCD_W - m, BSP_LCD_H - m, m, m, 0xFFFF);  // 右下 白

    // 中心十字
    fill_rect(fb, BSP_LCD_W / 2 - 1, 0, 2, BSP_LCD_H, 0xFFFF);
    fill_rect(fb, 0, BSP_LCD_H / 2 - 1, BSP_LCD_W, 2, 0xFFFF);

    // 底部 4 像素颜色条：红绿蓝白，检查通道顺序
    for (int x = 0; x < BSP_LCD_W; x++) {
        uint16_t c = (x < BSP_LCD_W / 4)       ? 0xF800
                   : (x < BSP_LCD_W / 2)       ? 0x07E0
                   : (x < BSP_LCD_W * 3 / 4)   ? 0x001F
                                               : 0xFFFF;
        for (int y = BSP_LCD_H - 4; y < BSP_LCD_H; y++) fb[y * BSP_LCD_W + x] = c;
    }

    esp_err_t e = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BSP_LCD_W, BSP_LCD_H, fb);
    ESP_LOGI(TAG, "诊断画面绘制: %s（四角 红绿蓝白，中心十字，底部颜色条）",
             e == ESP_OK ? "OK" : esp_err_to_name(e));
    heap_caps_free(fb);
}

esp_err_t bsp_display_set_on(bool on)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_disp_on_off(s_panel, on);
}

esp_err_t bsp_display_fill(const uint16_t *frame)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (!frame) return ESP_ERR_INVALID_ARG;
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BSP_LCD_W, BSP_LCD_H, frame);
}
