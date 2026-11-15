// 主机侧 shim：日志宏空实现。
#pragma once
#define ESP_LOGI(tag, fmt, ...) ((void)(tag), (void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)(tag), (void)0)
#define ESP_LOGE(tag, fmt, ...) ((void)(tag), (void)0)
