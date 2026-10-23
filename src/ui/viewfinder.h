// The photo viewfinder overlay from the mockup: frame brackets, split-image
// circle, level line, film name, light meter, frame counter tape and date.
#pragma once
#include <stdint.h>
#include "../film/film.h"
#include "../gfx/canvas.h"

namespace ui {

struct Exposure {
  float err;          // stops off from a good exposure: negative = too dark
  const char* label;  // shutter speed as a viewfinder shows it, e.g. "125"
  bool shake;         // slow enough that hand shake will blur the picture
};
// Picks the shutter speed an f/2.8 camera would use for this light and film.
Exposure meter(float lux, int iso);

struct ViewState {
  const film::Stock* stock;
  bool noFilm;
  int frame, rollLen;    // frame in the gate, exposures on the roll
  float counterOffset;   // 0..1 while the counter rolls to the next frame
  bool dateOn;
  float lux;             // negative if the light sensor is missing
  float tiltDeg;
  bool levelValid;       // false when the camera points at the sky or the floor
  bool needWind;         // shutter locked until B winds on
  bool rollDone;         // last frame shot: B rewinds
  float windP;           // 0..1 while winding (sprocket strip), otherwise 0
  bool countDown;        // instant pack: the counter shows shots left
  bool flashOn;          // flash enabled: show the bolt
  bool flashCharging;    // still charging after the last flash: the bolt blinks
  uint32_t nowMs;
};

void drawViewfinder(gfx::Canvas& c, const ViewState& s);

// A small film canister icon, 7x10 px.
void miniCanister(gfx::Canvas& c, int x, int y, const film::Stock& st);

}  // namespace ui
