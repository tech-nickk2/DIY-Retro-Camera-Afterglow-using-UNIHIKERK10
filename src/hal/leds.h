// The three WS2812 LEDs, driven by the RMT peripheral.
#pragma once
#include <stdint.h>

namespace leds {

bool begin();
void set(int i, uint8_t r, uint8_t g, uint8_t b);
void setAll(uint8_t r, uint8_t g, uint8_t b);
void show();

}  // namespace leds
