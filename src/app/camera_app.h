// The Afterglow camera. Stage 2, step 4: instant packs, cine mode, delete.
//
// Photo viewfinder:
//   A        shutter (locked until wound on); starts the self-timer if set
//   B        wind on; rewind at the end of a roll; new pack when an instant pack is empty
//   hold B   settings        hold A   album        A+B   switch to cine
// Cine viewfinder:
//   A        start / stop recording
//   B        switch Super 8 <-> camcorder
//   hold B   settings        hold A   album        A+B   switch to photo
// Album and full view:
//   A+B      delete (asks first)
#pragma once

namespace cameraApp {
void setup();
void loop();
}
