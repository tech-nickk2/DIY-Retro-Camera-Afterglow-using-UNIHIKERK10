// Camera sounds, synthesised on the fly from the same recipes as the
// mockup's Web Audio code: filtered noise bursts, pitch-swept thunks, tones
// and a small motor. A task plays them one after another, so play() never
// blocks the screen.
#pragma once
#include <stdint.h>

namespace sound {

enum class Fx : uint8_t {
  Chime,         // power on
  ShutterOpen,   // first curtain
  ShutterClose,  // second curtain
  Wind,          // film advance ratchet
  Thud,          // shutter locked / nothing to do
  Tick,          // menu step
  Clunk,         // mechanism engaging
  Motor,         // short motor run
  MotorLong,     // rewind
  Beep,          // self-timer
  Ratchet,       // one click of the thumbwheel
  WindStop,      // the frame locks into place
  Crank,         // one click of the rewind crank
  FlashCharge,   // the rising whine of a flash capacitor charging
};

void begin();
void play(Fx fx);
void setEnabled(bool on);

}  // namespace sound
