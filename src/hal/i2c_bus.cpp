#include "i2c_bus.h"
#include "esp_log.h"
#include "../board/k10_pins.h"

static const char* TAG = "i2c";
static i2c_master_bus_handle_t s_bus = nullptr;
static constexpr int TIMEOUT_MS = 50;

namespace i2cbus {

bool begin() {
  if (s_bus) return true;
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = (i2c_port_num_t)K10_I2C_PORT;
  cfg.sda_io_num = (gpio_num_t)K10_I2C_SDA;
  cfg.scl_io_num = (gpio_num_t)K10_I2C_SCL;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;  // the board has 10k pull-ups too
  esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "bus init failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool probe(uint8_t addr) {
  return s_bus && i2c_master_probe(s_bus, addr, TIMEOUT_MS) == ESP_OK;
}

i2c_master_dev_handle_t add(uint8_t addr, uint32_t hz) {
  if (!probe(addr)) return nullptr;
  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = addr;
  dc.scl_speed_hz = hz;
  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(s_bus, &dc, &dev) != ESP_OK) return nullptr;
  return dev;
}

bool writeReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
  if (!dev) return false;
  uint8_t buf[2] = {reg, val};
  return i2c_master_transmit(dev, buf, 2, TIMEOUT_MS) == ESP_OK;
}

bool readRegs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out, size_t n) {
  if (!dev) return false;
  return i2c_master_transmit_receive(dev, &reg, 1, out, n, TIMEOUT_MS) == ESP_OK;
}

bool readReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out) {
  return readRegs(dev, reg, out, 1);
}

int scan() {
  int found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (probe(a)) {
      ESP_LOGI(TAG, "device at 0x%02X", a);
      found++;
    }
  }
  return found;
}

}  // namespace i2cbus
