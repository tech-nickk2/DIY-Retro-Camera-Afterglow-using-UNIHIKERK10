// KY-040 rotary encoder, decoded in hardware by the ESP32-S3 pulse counter
// (PCNT) with a glitch filter, so no click is lost on a fast spin. The push
// switch is read with the buttons (see buttons.h).
#pragma once
#include <stdint.h>

namespace encoder {

bool begin();
// Raw quadrature counts since the last call (about 4 per click on a KY-040).
int readDelta();
// Running raw count, for the bring-up screen.
int total();

}  // namespace encoder
