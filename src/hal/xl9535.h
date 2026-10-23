// XL9535 16-bit I/O expander (TCA9555 register map). It owns the LCD
// backlight, the camera reset line and both user buttons.
#pragma once
#include <stdint.h>

namespace xl9535 {

bool begin();
bool ok();
void write(int bit, bool high);
// Makes an expander pin an output (starting low).
void setOutput(int bit);
// Raw input port, bit n = expander pin n. Returns false on a bus error.
bool readInputs(uint16_t* out);

}  // namespace xl9535
