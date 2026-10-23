// External white flash LED on edge P8, through the expander. It is on/off
// only (the expander can't dim it), and stays lit for the whole exposure so
// the camera's auto exposure sees the light, like a video light or the
// "pre-flash" of later compacts.
#pragma once

namespace flash {

void begin();
void set(bool on);
bool isOn();

}  // namespace flash
