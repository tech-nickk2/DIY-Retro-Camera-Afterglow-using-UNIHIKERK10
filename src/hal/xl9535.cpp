#include "xl9535.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../board/k10_pins.h"

static const char* TAG = "xl9535";
static i2c_master_dev_handle_t s_dev = nullptr;
static uint16_t s_out = 0;
static uint16_t s_cfg = 0xFFFF;  // 1 = input
static SemaphoreHandle_t s_lock = nullptr;

enum : uint8_t { REG_IN0 = 0x00, REG_OUT0 = 0x02, REG_OUT1 = 0x03, REG_CFG0 = 0x06, REG_CFG1 = 0x07 };

namespace xl9535 {

bool begin() {
  if (s_dev) return true;  // main.cpp starts it early to read the buttons at boot
  s_dev = i2cbus::add(K10_ADDR_XL9535);
  if (!s_dev) {
    ESP_LOGE(TAG, "not found at 0x%02X", K10_ADDR_XL9535);
    return false;
  }
  s_lock = xSemaphoreCreateMutex();
  // Set output levels before switching pins to outputs so nothing glitches:
  // backlight off (until the first frame is drawn), camera out of reset.
  s_out = (1u << K10_EXP_CAM_RST);
  i2cbus::writeReg(s_dev, REG_OUT0, s_out & 0xFF);
  i2cbus::writeReg(s_dev, REG_OUT1, s_out >> 8);
  // 1 = input. P00 and P01 are outputs; everything else stays an input.
  s_cfg = 0xFFFF & ~((1u << K10_EXP_LCD_BL) | (1u << K10_EXP_CAM_RST));
  i2cbus::writeReg(s_dev, REG_CFG0, s_cfg & 0xFF);
  i2cbus::writeReg(s_dev, REG_CFG1, s_cfg >> 8);
  return true;
}

bool ok() { return s_dev != nullptr; }

void write(int bit, bool high) {
  if (!s_dev) return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  if (high) s_out |= (1u << bit); else s_out &= ~(1u << bit);
  if (bit < 8) i2cbus::writeReg(s_dev, REG_OUT0, s_out & 0xFF);
  else i2cbus::writeReg(s_dev, REG_OUT1, s_out >> 8);
  xSemaphoreGive(s_lock);
}

void setOutput(int bit) {
  if (!s_dev) return;
  write(bit, false);  // level first, then direction, so it never glitches high
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_cfg &= ~(1u << bit);
  if (bit < 8) i2cbus::writeReg(s_dev, REG_CFG0, s_cfg & 0xFF);
  else i2cbus::writeReg(s_dev, REG_CFG1, s_cfg >> 8);
  xSemaphoreGive(s_lock);
}

bool readInputs(uint16_t* out) {
  if (!s_dev) return false;
  uint8_t b[2];
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool ok = i2cbus::readRegs(s_dev, REG_IN0, b, 2);
  xSemaphoreGive(s_lock);
  if (ok) *out = b[0] | (b[1] << 8);
  return ok;
}

}  // namespace xl9535
