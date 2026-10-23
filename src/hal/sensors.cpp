#include "sensors.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "../board/k10_pins.h"

static const char* TAG = "sensors";
static i2c_master_dev_handle_t s_acc = nullptr, s_als = nullptr;
static float s_lastLux = -1;

namespace sensors {

bool begin() {
  s_acc = i2cbus::add(K10_ADDR_SC7A20H);
  if (s_acc) {
    uint8_t who = 0;
    i2cbus::readReg(s_acc, 0x0F, &who);
    ESP_LOGI(TAG, "SC7A20H WHO_AM_I 0x%02X (expect 0x11)", who);
    i2cbus::writeReg(s_acc, 0x20, 0x47);  // 50 Hz, X/Y/Z on
    i2cbus::writeReg(s_acc, 0x23, 0x88);  // block update, high resolution, +-2 g
  } else if (i2cbus::probe(K10_ADDR_MSA311)) {
    ESP_LOGW(TAG, "found an MSA311 at 0x62 instead of SC7A20H; not supported yet");
  } else {
    ESP_LOGW(TAG, "no accelerometer found");
  }

  s_als = i2cbus::add(K10_ADDR_LTR303);
  if (s_als) {
    uint8_t part = 0;
    i2cbus::readReg(s_als, 0x86, &part);
    ESP_LOGI(TAG, "LTR-303 PART_ID 0x%02X (expect 0xA0)", part);
    i2cbus::writeReg(s_als, 0x80, 0x01);  // active, gain 1x
    i2cbus::writeReg(s_als, 0x85, 0x03);  // 100 ms integration, 500 ms rate
  } else {
    ESP_LOGW(TAG, "no light sensor found");
  }
  return s_acc || s_als;
}

bool accelOk() { return s_acc != nullptr; }
bool lightOk() { return s_als != nullptr; }

bool accel(int16_t* x, int16_t* y, int16_t* z) {
  uint8_t b[6];
  if (!i2cbus::readRegs(s_acc, 0x28 | 0x80, b, 6)) return false;  // 0x80 = auto-increment
  // 12-bit left-justified, 1 mg per count at +-2 g in high resolution mode
  *x = (int16_t)(b[0] | (b[1] << 8)) >> 4;
  *y = (int16_t)(b[2] | (b[3] << 8)) >> 4;
  *z = (int16_t)(b[4] | (b[5] << 8)) >> 4;
  return true;
}

float lux() {
  uint8_t b[4];
  // CH1 must be read before CH0 (0x88..0x8B)
  if (!i2cbus::readRegs(s_als, 0x88, b, 4)) return s_lastLux;
  float ch1 = b[0] | (b[1] << 8), ch0 = b[2] | (b[3] << 8);
  if (ch0 + ch1 <= 0) return s_lastLux = 0;
  float ratio = ch1 / (ch0 + ch1), l;
  // LTR-303 datasheet formula, gain 1x, 100 ms integration
  if (ratio < 0.45f) l = 1.7743f * ch0 + 1.1059f * ch1;
  else if (ratio < 0.64f) l = 4.2785f * ch0 - 1.9548f * ch1;
  else if (ratio < 0.85f) l = 0.5926f * ch0 + 0.1185f * ch1;
  else l = 0;
  return s_lastLux = l < 0 ? 0 : l;
}

}  // namespace sensors
