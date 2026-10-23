// Film stocks and the look engine. Each stock becomes a 65,536-entry table
// that maps every possible RGB565 pixel straight to its film colour, so the
// live viewfinder costs one lookup per pixel. Grain and vignette are added
// in the same pass. Colour parameters match mockup/afterglow.html.
#pragma once
#include <stdint.h>
#include "../gfx/canvas.h"

namespace film {

struct Params {
  float sat, k;       // saturation, tone-curve contrast (1 = linear)
  float lift[3];      // black point per channel
  float gain[3];      // white point per channel
  float sh[3], hi[3]; // shadow and highlight tints
  float mono[3];      // luminance weights; all zero for colour film
};

struct Stock {
  const char* id;
  const char* name;
  int iso;
  const char* desc;
  uint16_t c1, c2;    // canister colours
  float grain;        // grain strength, in 8-bit levels
  float vig;          // vignette strength, 0..1
  int pack;           // shots per pack for instant film, 0 for a roll
  Params p;
};

extern const Stock STOCKS[];
extern const int STOCK_COUNT;
extern const Stock NO_FILM;  // the flat sensor image, shown when no film is loaded
// Cine looks (not on the film shelf): colour for cine mode, same engine.
extern const Stock SUPER8;
extern const Stock CAMCORDER;
extern const Stock SILENT;   // 1920s: sepia black-and-white
extern const Stock CCTV;     // security camera: grey-green, flat
extern const Stock POCKET;   // neutral; cinefx::pocket() does the four-shade palette
extern const Stock WEBCAM;   // early 2000s: oversaturated, cool

int indexOf(const char* id);

// Builds (or reuses) the stock's table and makes it current. Takes ~50 ms.
bool select(const Stock& s);
const Stock& current();
// Frees the 128 KB table (Wi-Fi needs the fast RAM); select() rebuilds it.
void release();
// True if the current table lives in internal RAM (fast) rather than PSRAM.
bool tableInternal();

// Applies the current film to a w x h buffer in place: colour, vignette,
// and grain scaled by grainScale. seed moves the grain between frames.
void apply(uint16_t* px, int w, int h, float grainScale, uint32_t seed);

// Burns the orange '26 9 24 date into the bottom-right corner, sized for the canvas.
void dateStamp(gfx::Canvas& c);
// The date text as printed, e.g. "'26 9 24"; empty if the clock is not set.
void dateText(char* out, int len);

}  // namespace film
