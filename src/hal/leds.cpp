#include "leds.h"
#include <Arduino.h>
#include "../board/k10_pins.h"

static uint8_t s_grb[K10_RGB_COUNT][3];
static rmt_data_t s_bits[K10_RGB_COUNT * 24];
static bool s_ok = false;

namespace leds {

bool begin() {
  // 10 MHz ticks: a 0 bit is 0.4 us high + 0.8 us low, a 1 bit is 0.8 + 0.4.
  s_ok = rmtInit(K10_RGB_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  if (s_ok) { setAll(0, 0, 0); show(); }
  return s_ok;
}

void set(int i, uint8_t r, uint8_t g, uint8_t b) {
  if (i < 0 || i >= K10_RGB_COUNT) return;
  s_grb[i][0] = g; s_grb[i][1] = r; s_grb[i][2] = b;
}

void setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < K10_RGB_COUNT; i++) set(i, r, g, b);
}

void show() {
  if (!s_ok) return;
  int k = 0;
  for (int i = 0; i < K10_RGB_COUNT; i++)
    for (int c = 0; c < 3; c++)
      for (int bit = 7; bit >= 0; bit--) {
        bool one = (s_grb[i][c] >> bit) & 1;
        s_bits[k].level0 = 1; s_bits[k].duration0 = one ? 8 : 4;
        s_bits[k].level1 = 0; s_bits[k].duration1 = one ? 4 : 8;
        k++;
      }
  rmtWrite(K10_RGB_PIN, s_bits, k, RMT_WAIT_FOR_EVER);
}

}  // namespace leds
