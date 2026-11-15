// 主机侧 shim：只提供 UI 预览需要的尺寸与帧提交接口。
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define BSP_UI_W 240
#define BSP_UI_H 135

typedef int esp_err_t;
#define ESP_OK 0

esp_err_t bsp_display_fill(const uint16_t *frame);
