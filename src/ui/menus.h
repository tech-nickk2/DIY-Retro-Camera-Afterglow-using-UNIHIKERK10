// Menu-type screens from the mockup: settings list, film shelf, album strip,
// full-screen print, date back, and the self-timer countdown.
#pragma once
#include <stdint.h>
#include <time.h>
#include "../app/gallery.h"
#include "../film/film.h"
#include "../gfx/canvas.h"

namespace ui {

struct MenuRow {
  const char* label;
  char value[24];
};
// Draws over whatever is already on the canvas (the live viewfinder), dimmed.
void drawMenu(gfx::Canvas& c, const MenuRow* rows, int n, int sel);

// sel: highlighted film; off: animated carousel position; loaded: index of the
// loaded film or -1; preview: 112x84 live view through the selected film.
void drawShelf(gfx::Canvas& c, int sel, float off, int loaded, bool forced, const uint16_t* preview, int rollLen);

// thumb(num) returns a 100x75 thumbnail or nullptr.
using ThumbFn = const uint16_t* (*)(uint16_t num);
void drawAlbum(gfx::Canvas& c, const gallery::Item* items, int n, int sel, float off, ThumbFn thumb);
void drawPhotoView(gfx::Canvas& c, const uint16_t* preview, const gallery::Item& it, int idx, int n);

// "Delete this photo?" over whatever is on the canvas.
// deleteChosen: which of KEEP / DELETE is highlighted.
void drawConfirm(gfx::Canvas& c, const char* l1, const char* l2, bool deleteChosen);

// field: 0 year, 1 month, 2 day, 3 hour, 4 minute.
void drawDateSet(gfx::Canvas& c, const struct tm& t, int field, uint32_t nowMs);

// Big countdown digits over the viewfinder.
void drawTimer(gfx::Canvas& c, int remainingMs);

}  // namespace ui
