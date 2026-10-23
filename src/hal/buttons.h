// Inputs: the KY-040 knob (turn, press, hold, press-and-turn) plus the
// K10's own A and B buttons as a backup. Everything is polled at 100 Hz on
// its own task and turned into gestures:
//   - a press fires on release, a hold fires at 600 ms
//   - holding A and B together for 650 ms fires AB
//   - turning the knob while it is held down fires PushTurn instead of a
//     press or a hold, so it can never be triggered by accident
#pragma once
#include <stdint.h>

enum class BtnEvent : uint8_t {
  None,
  A, B, A_Long, B_Long, AB,           // backup buttons
  Knob, KnobHold,                      // knob press (on release) and hold
  TurnCW, TurnCCW,                     // one click of the knob
  PushTurnCW, PushTurnCCW,             // one click while the knob is held down
};

namespace buttons {

bool begin();
// Next gesture, or BtnEvent::None if there is nothing waiting.
BtnEvent poll();
// Current debounced state, for the bring-up screen.
bool downA();
bool downB();
bool downKnob();
const char* name(BtnEvent e);

// reversed swaps the turn direction; countsPerClick is 4 for most KY-040s
// (2 for some encoders that report half steps).
void setKnob(bool reversed, int countsPerClick);

}  // namespace buttons
