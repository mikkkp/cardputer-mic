// components/bsp/include/bsp_i2c.h
// 内部 I2C 共享总线（G8/G9）：ES8311 0x18、TCA8418 0x34、BMI270 0x68。
// 与上游不同：本板无 CW2017 电量计，也无需 deep sleep 高阻切换。
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

// 初始化共享总线。幂等：重复调用直接返回 ESP_OK，每个驱动都可放心调用。
esp_err_t bsp_i2c_init(void);

// 取共享总线句柄；未初始化返回 NULL。
i2c_master_bus_handle_t bsp_i2c_bus(void);

// 扫描 0x08..0x77 并打印应答设备，标注 ES8311 / TCA8418。
// P0 用它验证引脚表：期望至少出现 0x18 与 0x34。
//
// ⚠ 不要为「隔离 NACK」另开临时总线扫描：同一 port 二次 i2c_new_master_bus()
//   的失败清理路径会把正式总线的 SDA/SCL 从 I2C 外设解绑（IDF 5.5.3 实测），
//   此后所有事务卡在 clear bus failed。直接在正式总线上 i2c_master_probe() 是安全的。
esp_err_t bsp_i2c_scan(void);
