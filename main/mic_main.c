// main/mic_main.c
// P2 主程序：移植上游 mic_app.c 的状态机与主循环，替换为新 BSP：
//   - bsp_button_init → bsp_keyboard_init + keymap 语义键（Ctrl=OK听写、Fn=DOWN回车、Tab=UP撤销）
//   - bsp_lvgl_* → mic_ui_stub（裸 framebuffer）
//   - bsp_audio_read → bsp_audio_read_mono（L 槽抽取内聚）
//   - bsp_battery_* → 2026-11-15 方案 A：SAR ADC 硬件不可信（5 版探针交叉验证），
//     改用库仑计数（mic_battery_model）估算电量。
// 协议逻辑（mic_protocol/mic_adpcm/mic_ble）零改动。
#include "bsp_audio.h"
#include "bsp_display.h"
#include "bsp_keyboard.h"
#include "bsp_keymap.h"
#include "mic_adpcm.h"
#include "mic_battery_model.h"
#include "mic_ble.h"
#include "mic_protocol.h"
#include "mic_screen.h"
#include "mic_ui_stub.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t buttons;
static mic_model_t model;
static bool ble_owner;
static uint32_t sequence;
static int16_t pcm[MIC_SAMPLES];
static uint8_t packet[MIC_HEADER_SIZE + MIC_PAYLOAD_MAX];
static mic_screen_t screen;

#define BACKLIGHT_ON  true
#define BACKLIGHT_DIM false  // 占位：无渐变，DIM 即灭背光（P3 再做 PWM）

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool send_frame(uint8_t type, const void *data, uint16_t size)
{
    uint8_t compressed[MIC_ADPCM_BYTES];
    if (ble_owner && type == MIC_AUDIO) {
        mic_adpcm_encode(data, compressed);
        type = MIC_AUDIO_ADPCM;
        data = compressed;
        size = sizeof(compressed);
    }
    size_t count = mic_packet(packet, type, sequence++, data, size);
    if (ble_owner) {
        bool droppable = type == MIC_AUDIO_ADPCM || type == MIC_HELLO || type == MIC_POWER;
        esp_err_t err = mic_ble_send(packet, count, droppable);
        return err == ESP_OK || (droppable && err != ESP_ERR_INVALID_STATE);
    }
    if (!usb_serial_jtag_is_connected()) return false;
    size_t sent = 0;
    while (sent < count) {
        int n = usb_serial_jtag_write_bytes(packet + sent, count - sent, pdMS_TO_TICKS(100));
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

static void apply_screen(mic_screen_level_t level, mic_screen_level_t shown)
{
    if (level == shown) return;
    if (level == MIC_SCREEN_OFF) {
        bsp_display_backlight(false);
        (void)bsp_display_set_on(false);
        return;
    }
    if (shown == MIC_SCREEN_OFF) (void)bsp_display_set_on(true);
    bsp_display_backlight(level == MIC_SCREEN_DIM ? BACKLIGHT_DIM : BACKLIGHT_ON);
}

// 键盘事件回调（ISR/驱动任务上下文）：只入队，不阻塞。
// 所有按键都入队（含非语义键）：主循环里 mic_screen_key() 需要「任意键唤醒屏幕」；
// 非语义键入队为 BSP_BTN_COUNT，不匹配 OK/UP/DOWN 任何分支，只点亮不动作。
static void on_key(const bsp_key_raw_t *raw, const bsp_key_t *key, bsp_btn_t btn)
{
    (void)raw;
    /* 只入队按下沿，与上游 key_event 的 BSP_BTN_PRESS 过滤一致；
       否则一次物理按压产生 DOWN+UP 两条事件，UP 会在 RECORDING 态立刻发 STOP。 */
    if (!key->pressed) return;
    uint8_t code = (uint8_t)btn;
    xQueueSend(buttons, &code, 0);
}

/* 电量（方案 A，库仑计数）：
   SAR ADC 硬件不可信 → 不读电压。mic_battery_model 用 NVS 持久化 +
   放电递减 + 充电回充 + 满电校准维护 mAh 估计。
   POWER 帧：soc_q8 用估算值；mv 无真实来源传 -1。
   UI：百分比来自库仑计数。 */
static int battery_mv_probe(void)     { return -1; }
static int battery_soc_q8_probe(void) { return mic_battery_model_soc_q8(); }
static int battery_soc_probe(void)    { return mic_battery_model_soc(); }

/* 屏幕可视化日志：显示初始化成功后，把启动阶段直接画到屏幕（串口会复位芯片，取不到日志） */
static bool s_disp_ok = false;
#define STAGE(a, b) do { \
        ESP_LOGW("mic", "[boot] %s %s", (a), (b) ? (b) : ""); \
        if (s_disp_ok) mic_ui_boot_stage((a), (b)); \
    } while (0)

void app_main(void)
{
    /* 屏幕可视化日志：console=NONE（USB-JTAG console 早期写日志会卡死；GPIO43 被
       I2S 占用），串口日志保留但常态无输出，诊断走 mic_ui_boot_stage 画到屏幕上。 */
    ESP_LOGW("mic", "[boot] app_main start, rr=%d", (int)esp_reset_reason());

    buttons = xQueueCreate(8, sizeof(uint8_t));
    ESP_ERROR_CHECK(buttons ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(bsp_display_init());
    mic_ui_create();
    bsp_display_backlight(BACKLIGHT_ON);
    s_disp_ok = true;
    STAGE("1 disp", "ok");

    bool audio_ok = bsp_audio_init() == ESP_OK;
    STAGE("2 audio", audio_ok ? "ok" : "FAIL");

    /* P3-3 定版 + 2026-11-15 方案 A：不再初始化 SAR ADC。
       仍保留 1.5s 等待让 IDF 连接监视器收敛，供 usb 判定使用。 */
    vTaskDelay(pdMS_TO_TICKS(1500));
    bool usb_here = usb_serial_jtag_is_connected();
    if (!usb_here) {
        vTaskDelay(pdMS_TO_TICKS(200));
        usb_here = usb_serial_jtag_is_connected();
    }

    /* 库仑计数初始化：需 NVS。mic_ble_init 里也会 init，但这里先做
       才能恢复电池模型（幂等：第二次调用返回 ESP_OK）。 */
    (void)nvs_flash_init();
    mic_battery_model_init();
    STAGE("3 battery", usb_here ? "usb:powered" : "bat:coulomb");


    ESP_ERROR_CHECK(bsp_keyboard_init());
    bsp_keyboard_set_event_cb(on_key);
    STAGE("4 kbd", "ok");

    /* 不调用 usb_serial_jtag_driver_install()：P2 实测在 macOS 上装驱动会
       detach/attach USB 外设导致设备从总线消失。 */

    STAGE("5 ble", "init...");
    esp_err_t ble_err = mic_ble_init();
    STAGE("6 ble", esp_err_to_name(ble_err));
    if (ble_err != ESP_OK) model.state = MIC_OFFLINE;

    uint32_t last_hello = 0, last_ui = 0, last_battery = 0, last_power = 0, last_bat_tick = 0;
    bool power_due = true;
    int battery = battery_soc_probe(), ui_peak = 0;
    mic_screen_wake(&screen, now_ms());
    mic_screen_level_t shown_screen = MIC_SCREEN_ON;
    mic_state_t last_state = model.state;

    for (;;) {
        uint32_t now = now_ms();
        uint8_t commands[32];

        if (mic_ble_lost()) {
            if (ble_owner) model.state = MIC_OFFLINE;
            ble_owner = false;
        }
        if (mic_ble_connected() != ble_owner) {
            model.state = MIC_OFFLINE;
            ble_owner = mic_ble_connected();
        }
        int count = 0;  /* USB 帧协议驱动未装（见 app_main 注释），serial 命令链路暂停 */
        if (ble_owner) count = 0;  /* 双宿主不混控制命令 */
        for (int i = 0; i < count; ++i) {
            if (commands[i] == 'P') mic_ping(&model, now);
            if (commands[i] == 'A') (void)mic_ack(&model, now);
            if (commands[i] == 'X') model.state = MIC_OFFLINE;
        }
        unsigned ble_commands = mic_ble_take_commands();
        if (ble_owner) {
            if (ble_commands & MIC_BLE_CMD_RESET) model.state = MIC_OFFLINE;
            if (ble_commands & MIC_BLE_CMD_PING) mic_ping(&model, now);
            if (ble_commands & MIC_BLE_CMD_ACK) (void)mic_ack(&model, now);
        }
        if (mic_expire(&model, now)) (void)send_frame(MIC_STOP, NULL, 0);

        uint8_t pressed;
        while (xQueueReceive(buttons, &pressed, 0) == pdTRUE) {
            bool act = mic_screen_key(&screen, now, pressed == BSP_BTN_OK);
            uint8_t event = 0;
            uint32_t duration = now - model.started;
            if (!act) event = 0;
            else if (pressed == BSP_BTN_OK) event = audio_ok ? mic_press(&model, now) : MIC_ERROR;
            else if (pressed == BSP_BTN_UP) event = mic_edit_action(&model, true);
            else if (pressed == BSP_BTN_DOWN) event = mic_edit_action(&model, false);
            bool sent = !event || send_frame(event, NULL, 0);
            if (!sent) model.state = MIC_OFFLINE;
            if (act && pressed == BSP_BTN_OK) mic_ui_press(now);
            if (sent && (event == MIC_UNDO || event == MIC_CONFIRM)) mic_ui_action(now, event);
            if (sent && event == MIC_STOP) mic_ui_finished(now, duration);
        }

        int peak = 0;
        /* 持续排水 I2S：空闲 PCM 本地丢弃，保证新录音不以陈旧 DMA 音频开头 */
        if (audio_ok) {
            if (bsp_audio_read_mono(pcm, MIC_SAMPLES) != ESP_OK) {
                audio_ok = false;
                model.state = MIC_OFFLINE;
                (void)send_frame(MIC_ERROR, NULL, 0);
            } else if (model.state == MIC_RECORDING) {
                for (int i = 0; i < MIC_SAMPLES; ++i) {
                    int value = pcm[i] < 0 ? -(int)pcm[i] : pcm[i];
                    if (value > peak) peak = value;
                }
                if (!send_frame(MIC_AUDIO, pcm, sizeof(pcm))) model.state = MIC_OFFLINE;
            }
        } else vTaskDelay(pdMS_TO_TICKS(20));

        if ((uint32_t)(now - last_hello) >= 500) {
            uint8_t state = audio_ok ? model.state : 255;
            if (!send_frame(MIC_HELLO, &state, 1)) model.state = MIC_OFFLINE;
            last_hello = now;
        }
        if (model.state == MIC_OFFLINE) power_due = true;
        else if (power_due || (uint32_t)(now - last_power) >= 60000) {
            uint8_t report[MIC_POWER_SIZE];
            mic_power_payload(report, battery_soc_q8_probe(), battery_mv_probe(),
                              audio_ok ? (uint8_t)model.state : 255,
                              (uint8_t)screen.level,
                              usb_serial_jtag_is_connected());
            /* P3-3 诊断：POWER 帧第 8 字节是协议保留位，Mac 端 parse_power 用
               <HHBBBx 显式跳过，填最低空闲堆（KB，钳到 255）不改动帧长、
               已定义字段与 CRC，Mac 端零改动。压力测试据此看堆水位是否下滑。 */
            {
                uint32_t low = esp_get_minimum_free_heap_size() / 1024;
                report[7] = (uint8_t)(low > 255 ? 255 : low);
            }
            if (!send_frame(MIC_POWER, report, sizeof(report))) model.state = MIC_OFFLINE;
            power_due = false;
            last_power = now;
        }
        if ((uint32_t)(now - last_battery) >= 5000) {
            battery = battery_soc_probe();
            last_battery = now;
        }

        /* 库仑计数：每秒驱动一次，放电三档 / 充电回充。 */
        if ((uint32_t)(now - last_bat_tick) >= 1000) {
            mic_battery_model_tick(usb_serial_jtag_is_connected(),
                                   model.state == MIC_RECORDING,
                                   screen.level != MIC_SCREEN_OFF);
            battery = battery_soc_probe();
            last_bat_tick = now;
        }

        if (peak > ui_peak) ui_peak = peak;
        if ((uint32_t)(now - last_ui) >= 50) {
            if (model.state != last_state) mic_screen_wake(&screen, now);
            last_state = model.state;
            bool busy = model.state == MIC_STARTING || model.state == MIC_RECORDING ||
                        mic_ble_passkey() >= 0;
            mic_screen_level_t level = mic_screen_tick(&screen, now, busy);
            mic_ui_link(ble_owner, mic_ble_passkey());
            if (level != MIC_SCREEN_OFF) {
                mic_ui_update(&model, now, ui_peak, audio_ok, battery);
                apply_screen(level, shown_screen);
                shown_screen = level;
            } else {
                apply_screen(level, shown_screen);
                shown_screen = level;
            }
            ui_peak = 0;
            last_ui = now;
        }
        (void)bsp_keyboard_poll();  // 驱动 FIFO → 事件回调（只入队）
    }
}
