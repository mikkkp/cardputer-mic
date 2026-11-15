"""校验 bsp_pins.h 与官方 Cardputer ADV 引脚表一致，并防止引脚冲突/错位回退。

事实来源：官方产品页 PinMap / Product Comparison 表 + 本地官方 UserDemo 源码。
交接文档 §3.1 曾把 microSD 的 G12/G14/G40/G39 误标为 I2C/I2S，这里显式防回退。
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PINS = ROOT / "components/bsp/include/bsp_pins.h"
BLE_HEADER = ROOT / "main/mic_ble.h"

# 官方 PinMap 的关键值（只列本工程真正使用的部分）。
EXPECTED = {
    "BSP_LCD_W": "240",
    "BSP_LCD_H": "135",
    "BSP_LCD_BL": "38",
    "BSP_LCD_RST": "33",
    "BSP_LCD_DC": "34",
    "BSP_LCD_MOSI": "35",
    "BSP_LCD_SCLK": "36",
    "BSP_LCD_CS": "37",
    "BSP_I2C_PORT": "0",
    "BSP_I2C_SDA": "8",
    "BSP_I2C_SCL": "9",
    "BSP_I2C_ES8311_ADDR": "0x18",
    "BSP_I2C_TCA8418_ADDR": "0x34",
    "BSP_KEY_INT_GPIO": "11",
    "BSP_BAT_ADC_GPIO": "10",
    "BSP_BAT_ADC_CHANNEL": "9",
    "BSP_BAT_ADC_RATIO_NUM": "2",
    "BSP_BAT_ADC_RATIO_DEN": "1",
    "BSP_I2S_BCLK": "41",
    "BSP_I2S_WS": "43",
    "BSP_I2S_DOUT": "42",
    "BSP_I2S_DIN": "46",
    "BSP_I2S_MCLK": "(-1)",
    "BSP_IR_TX_GPIO": "44",
    "BSP_SD_CS_GPIO": "12",
    "BSP_SD_MOSI_GPIO": "14",
    "BSP_SD_CLK_GPIO": "40",
    "BSP_SD_MISO_GPIO": "39",
}


def load_macros():
    text = PINS.read_text(encoding="utf-8")
    return {
        m.group(1): m.group(2)
        for m in re.finditer(r"^\s*#define\s+(BSP_\w+)\s+([^\s/]+)", text, re.M)
    }


class PinTableTests(unittest.TestCase):
    def setUp(self):
        self.macros = load_macros()

    def test_expected_values(self):
        for name, value in EXPECTED.items():
            with self.subTest(macro=name):
                self.assertIn(name, self.macros)
                self.assertEqual(self.macros[name], value)

    def test_no_gpio_collision_between_used_peripherals(self):
        keys = [
            "BSP_LCD_BL", "BSP_LCD_RST", "BSP_LCD_DC",
            "BSP_LCD_MOSI", "BSP_LCD_SCLK", "BSP_LCD_CS",
            "BSP_I2C_SDA", "BSP_I2C_SCL",
            "BSP_I2S_BCLK", "BSP_I2S_WS", "BSP_I2S_DOUT", "BSP_I2S_DIN",
            "BSP_KEY_INT_GPIO", "BSP_BAT_ADC_GPIO",
        ]
        values = [self.macros[k] for k in keys]
        self.assertEqual(len(values), len(set(values)), f"引脚冲突: {values}")

    def test_sd_pins_not_mistaken_for_i2c_or_i2s(self):
        """交接文档 §3.1 的错误：microSD 的 SPI 脚被当成内部 I2C / ES8311 I2S。"""
        sd = {self.macros[k] for k in (
            "BSP_SD_CS_GPIO", "BSP_SD_MOSI_GPIO",
            "BSP_SD_CLK_GPIO", "BSP_SD_MISO_GPIO",
        )}
        ic_and_audio = {self.macros[k] for k in (
            "BSP_I2C_SDA", "BSP_I2C_SCL", "BSP_I2S_BCLK",
            "BSP_I2S_WS", "BSP_I2S_DOUT", "BSP_I2S_DIN", "BSP_KEY_INT_GPIO",
        )}
        self.assertTrue(sd.isdisjoint(ic_and_audio))
        self.assertEqual(self.macros["BSP_I2C_SDA"], "8")
        self.assertEqual(self.macros["BSP_I2C_SCL"], "9")

    def test_ble_device_name_is_distinct(self):
        text = BLE_HEADER.read_text(encoding="utf-8")
        self.assertIn('#define MIC_BLE_DEVICE_NAME "Cardputer Mic"', text)
        self.assertNotIn('"Passport Mic"', text)

    def test_service_uuid_unchanged(self):
        """Mac companion 只按服务 UUID 发现设备，UUID 不能改。"""
        text = BLE_HEADER.read_text(encoding="utf-8")
        self.assertIn("12345678-1234-5678-1234-56789abcdef0", text)
        self.assertIn("12345678-1234-5678-1234-56789abcdef1", text)
        self.assertIn("12345678-1234-5678-1234-56789abcdef2", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
