#include "flash.h"
#include "xl9535.h"
#include "../board/k10_pins.h"

static volatile bool s_on = false;

namespace flash {

void begin() {
  xl9535::setOutput(K10_EXP_FLASH);
  s_on = false;
}

void set(bool on) {
  if (on == s_on) return;  // skip the I2C write when nothing changes
  s_on = on;
  xl9535::write(K10_EXP_FLASH, on);
}

bool isOn() { return s_on; }

}  // namespace flash
