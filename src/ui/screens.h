// Full-screen scenes from the mockup: boot, darkroom, "lights on" review,
// rewind and film loading. Each draws one frame for time t (seconds since the
// scene started), so the app controls the timing.
#pragma once
#include <stdint.h>
#include "../film/film.h"
#include "../gfx/canvas.h"

namespace ui {

void drawBoot(gfx::Canvas& c, float t, const char* statusLine);

// print: the developed photo at 320x240. p: 0..1 development progress.
void drawDarkroom(gfx::Canvas& c, const uint16_t* print, float p, float t);

// "Lights on": the print in true colour, fading in from the safelight red.
void drawReview(gfx::Canvas& c, const uint16_t* print, float t, int frame, const film::Stock& st);

// Counter running back from `from` to S.
void drawRewind(gfx::Canvas& c, float p, int from, float t);

// Canister slides in, leader goes onto the take-up spool, counter goes S -> 1.
void drawLoading(gfx::Canvas& c, float t, const film::Stock& st);

// An instant print: white frame with a wide bottom border, top edge at fy.
// fog 0..1 is how much of the blue-grey haze is still on it; caption may be null.
// print is 320x240 with the square photo in its middle 240 columns.
void drawInstantPrint(gfx::Canvas& c, const uint16_t* print, int fy, float fog, const char* caption);
// The print ejecting from the top of the screen and developing.
void drawInstantDevelop(gfx::Canvas& c, const uint16_t* print, float t, float ejectS, float developS);

// Iris closing then opening over what is on the canvas; label shows at the middle.
void drawModeSwitch(gfx::Canvas& c, float t, const char* label, const char* sub);

// A film canister (or instant pack) at scale s, about 24x26 units.
void drawCanister(gfx::Canvas& c, int x, int y, int s, const film::Stock& st);

}  // namespace ui
