// SC7A20H accelerometer (tilt and level) and LTR-303 ambient light sensor
// (the light meter).
#pragma once
#include <stdint.h>

namespace sensors {

bool begin();
bool accelOk();
bool lightOk();

// Acceleration in milli-g. Returns false if the read failed.
bool accel(int16_t* x, int16_t* y, int16_t* z);
// Ambient light in lux; negative if unavailable.
float lux();

}  // namespace sensors
