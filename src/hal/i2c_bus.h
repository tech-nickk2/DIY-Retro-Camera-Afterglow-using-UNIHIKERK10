// One I2C master bus for every on-board device, using the ESP-IDF 5 driver.
// The camera attaches to the same bus by port number, so nothing else may
// install a second driver on these pins (no Wire.begin() on 47/48).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"

namespace i2cbus {

bool begin();
bool probe(uint8_t addr);
// Returns nullptr if the device does not answer.
i2c_master_dev_handle_t add(uint8_t addr, uint32_t hz = 400000);

bool writeReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);
bool readRegs(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out, size_t n);
bool readReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out);

// Logs every address that answers; returns how many were found.
int scan();

}  // namespace i2cbus
