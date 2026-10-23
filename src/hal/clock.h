// Wall clock without an RTC battery or network: starts from the later of the
// firmware build time and the last time saved to flash, then keeps saving
// once a minute. Like an old camera's date back, it needs setting by hand to
// be exact, and it stands still while the camera is off.
#pragma once
#include <time.h>

namespace clock_ {

void begin();
void tick();          // call from the main loop
void set(time_t t);   // for a future date-setting screen

}  // namespace clock_
