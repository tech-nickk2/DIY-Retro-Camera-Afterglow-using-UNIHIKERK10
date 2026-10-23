#include "cine_fx.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_random.h"
#include "../gfx/canvas.h"

using gfx::rgb;
using gfx::swap16;

namespace cinefx {

static constexpr int MAX_W = 320;

static inline uint32_t rnd() { return esp_random(); }

// Scales a panel-order pixel's brightness by k/256.
static inline uint16_t scale(uint16_t p, int k) {
  const uint16_t v = swap16(p);
  int r = ((v >> 11) * k) >> 8, g = (((v >> 5) & 63) * k) >> 8, b = ((v & 31) * k) >> 8;
  r = r > 31 ? 31 : r;
  g = g > 63 ? 63 : g;
  b = b > 31 ? 31 : b;
  return swap16((uint16_t)((r << 11) | (g << 5) | b));
}

void super8(uint16_t* px, int w, int h) {
  if (w > MAX_W) return;
  // Gate weave: the frame sits a pixel off now and then, rarely two down.
  const int jx = (int)(rnd() % 3) - 1;
  const int jy = rnd() % 12 == 0 ? 2 : (int)(rnd() % 2);
  // Flicker: the projector lamp is never quite steady.
  const int flicker = 240 + (int)(rnd() % 26);
  // The gate: black border with rounded corners (inset 7 px, radius 12).
  const int inset = 7, rad = 12;
  static uint16_t row[MAX_W];
  const uint16_t black = rgb(5, 4, 3);
  // Rows are shifted down by jy, so walk bottom-up and read rows that are not yet written.
  for (int y = h - 1; y >= 0; y--) {
    const int sy = y - jy;
    if (sy < 0) { for (int x = 0; x < w; x++) px[y * w + x] = black; continue; }
    memcpy(row, px + sy * w, w * 2);
    int left = inset;
    const int top = inset - 1, bottom = h - inset;
    if (y < top || y >= bottom) left = w;  // outside the gate vertically
    else {
      const int dy = y < top + rad ? top + rad - y : y >= bottom - rad ? y - (bottom - rad - 1) : 0;
      if (dy) {
        int dx = 0;
        while (dx * dx + dy * dy < rad * rad && dx < rad) dx++;
        left = inset + (rad - dx);
      }
    }
    uint16_t* d = px + y * w;
    for (int x = 0; x < w; x++) {
      if (x < left || x >= w - left) { d[x] = black; continue; }
      const int sx = x - jx;
      d[x] = scale(row[sx < 0 ? 0 : sx >= w ? w - 1 : sx], flicker);
    }
  }
  // Dust and the odd hair on the film.
  const uint16_t dust = rgb(20, 14, 10);
  const int specks = rnd() % 5 < 3 ? (int)(rnd() % 3) : 0;
  for (int k = 0; k < specks; k++) {
    const int x = inset + (int)(rnd() % (w - 2 * inset - 2)), y = inset + (int)(rnd() % (h - 2 * inset - 2));
    const int s = 1 + (int)(rnd() % 2);
    for (int j = 0; j < s; j++)
      for (int i = 0; i < s; i++) px[(y + j) * w + x + i] = dust;
  }
  if (rnd() % 25 == 0) {
    int x = inset + 10 + (int)(rnd() % (w - 2 * inset - 20)), y = inset + 10 + (int)(rnd() % (h - 2 * inset - 40));
    for (int k = 0; k < 24; k++) {  // a hair: a short wandering line
      px[y * w + x] = dust;
      y++;
      x += (int)(rnd() % 3) - 1;
      if (x < inset || x >= w - inset || y >= h - inset) break;
    }
  }
  if (rnd() % 30 == 0) {  // a bright speck: a scratch in the emulsion
    const int x = inset + (int)(rnd() % (w - 2 * inset)), y = inset + (int)(rnd() % (h - 2 * inset));
    px[y * w + x] = rgb(255, 248, 230);
  }
}

static uint32_t s_bandUntil = 0;
static int s_bandY = 0, s_bandH = 0;

void camcorder(uint16_t* px, int w, int h, uint32_t nowMs) {
  if (w > MAX_W) return;
  if (!s_bandUntil && rnd() % 160 == 0) {
    s_bandY = h * 7 / 10 + (int)(rnd() % (h / 5));
    s_bandH = 4 + (int)(rnd() % 8);
    s_bandUntil = nowMs + 250 + rnd() % 400;
  }
  if (s_bandUntil && nowMs > s_bandUntil) s_bandUntil = 0;
  static uint16_t row[MAX_W];
  for (int y = 0; y < h; y++) {
    uint16_t* d = px + y * w;
    memcpy(row, d, w * 2);
    const bool band = s_bandUntil && y >= s_bandY && y < s_bandY + s_bandH;
    const int shift = band ? (int)(rnd() % 10) - 5 : 0;
    const int dark = (y & 1) ? 228 : 256;  // scanlines
    for (int x = 0; x < w; x++) {
      // Chroma bleed: red lags a pixel, blue leads one.
      auto at = [&](int i) { return swap16(row[i < 0 ? 0 : i >= w ? w - 1 : i]); };
      int r = at(x - 1 + shift) >> 11, g = (at(x + shift) >> 5) & 63, b = at(x + 1 + shift) & 31;
      if (band && rnd() % 4 == 0) { r = 22 + rnd() % 10; g = r * 2; b = r; }
      r = (r * dark) >> 8;
      g = (g * dark) >> 8;
      b = (b * dark) >> 8;
      d[x] = swap16((uint16_t)((r << 11) | (g << 5) | b));
    }
  }
}

void burnCamcorderDate(uint16_t* px, int w, int h) {
  static const char* MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_year < 120) return;
  char l1[16], l2[20];
  int hr = t.tm_hour % 12;
  snprintf(l1, sizeof(l1), "%d:%02d %s", hr ? hr : 12, t.tm_min, t.tm_hour >= 12 ? "PM" : "AM");
  snprintf(l2, sizeof(l2), "%s.%d %d", MON[t.tm_mon], t.tm_mday, t.tm_year + 1900);
  gfx::Canvas c;
  c.wrap(px, w, h);
  c.text(10, h - 30, l1, gfx::col::WHITE, 1);
  c.text(10, h - 18, l2, gfx::col::WHITE, 1);
}

// ------------------------------------------------------------------ silent film

void silent(uint16_t* px, int w, int h) {
  if (w > MAX_W) return;
  static uint16_t row[MAX_W];
  // A hand-cranked projector: the frame jumps more and the lamp breathes.
  const int jx = (int)(rnd() % 5) - 2, jy = rnd() % 6 == 0 ? 3 : (int)(rnd() % 2);
  const int flicker = 205 + (int)(rnd() % 70);
  const uint16_t black = rgb(8, 6, 4);
  for (int y = h - 1; y >= 0; y--) {
    const int sy = y - jy;
    uint16_t* d = px + y * w;
    if (sy < 0 || y < 4 || y >= h - 4) { for (int x = 0; x < w; x++) d[x] = black; continue; }
    memcpy(row, px + sy * w, w * 2);
    for (int x = 0; x < w; x++) {
      if (x < 5 || x >= w - 5) { d[x] = black; continue; }
      const int sx = x - jx;
      d[x] = scale(row[sx < 0 ? 0 : sx >= w ? w - 1 : sx], flicker);
    }
  }
  // Scratches: a couple of bright lines that wander and last a few frames.
  static int sx[2] = {-1, -1}, life[2] = {0, 0};
  for (int k = 0; k < 2; k++) {
    if (life[k] <= 0 && rnd() % 8 == 0) { sx[k] = 10 + (int)(rnd() % (w - 20)); life[k] = 4 + (int)(rnd() % 14); }
    if (life[k] <= 0) continue;
    life[k]--;
    sx[k] += (int)(rnd() % 3) - 1;
    const int y0 = (int)(rnd() % (h / 3)), y1 = h - (int)(rnd() % (h / 3));
    for (int y = y0; y < y1; y++)
      if (sx[k] > 4 && sx[k] < w - 5) px[y * w + sx[k]] = rgb(235, 225, 200);
  }
  // Dust, and plenty of it.
  const int specks = (int)(rnd() % 6);
  for (int k = 0; k < specks; k++) {
    const int x = 6 + (int)(rnd() % (w - 14)), y = 6 + (int)(rnd() % (h - 14)), s = 1 + (int)(rnd() % 3);
    const uint16_t c = rnd() % 3 ? rgb(15, 10, 6) : rgb(240, 230, 210);
    for (int j = 0; j < s; j++)
      for (int i = 0; i < s; i++) px[(y + j) * w + x + i] = c;
  }
}

// ------------------------------------------------------------------ CCTV

void cctv(uint16_t* px, int w, int h) {
  for (int y = 1; y < h; y += 2) {
    uint16_t* d = px + y * w;
    for (int x = 0; x < w; x++) d[x] = scale(d[x], 232);
  }
  for (int k = 0; k < 60; k++) {  // sensor noise
    const int i = (int)(rnd() % (w * h));
    px[i] = scale(px[i], 180 + (int)(rnd() % 150));
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  char ts[48];
  snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
           t.tm_min, t.tm_sec);
  gfx::Canvas c;
  c.wrap(px, w, h);
  c.text(6, 6, "CAM 01", gfx::col::WHITE, 1);
  c.text(6, h - 13, ts, gfx::col::WHITE, 1);
}

// ------------------------------------------------------------------ pocket camera

void pocket(uint16_t* px, int w, int h) {
  // The four greens of an early handheld console screen, darkest first.
  static const uint16_t PAL[4] = {rgb(0x0F, 0x38, 0x0F), rgb(0x30, 0x62, 0x30), rgb(0x8B, 0xAC, 0x0F),
                                  rgb(0x9B, 0xBC, 0x0F)};
  static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (int by = 0; by + 1 < h; by += 2) {
    for (int bx = 0; bx + 1 < w; bx += 2) {
      // Brightness of the 2x2 block (0..255).
      int sum = 0;
      for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
          const uint16_t v = swap16(px[(by + j) * w + bx + i]);
          sum += ((v >> 11) << 3) * 3 + (((v >> 5) & 63) << 2) * 6 + ((v & 31) << 3);
        }
      const int lum = sum / 40;
      const int t = BAYER[(by / 2) & 3][(bx / 2) & 3];
      int level = (lum * 3 + (t - 8) * 12) / 255;
      level = level < 0 ? 0 : level > 3 ? 3 : level;
      const uint16_t c = PAL[level];
      px[by * w + bx] = px[by * w + bx + 1] = px[(by + 1) * w + bx] = px[(by + 1) * w + bx + 1] = c;
    }
  }
}

// ------------------------------------------------------------------ webcam

void webcam(uint16_t* px, int w, int h) {
  // Average each 2x2 block and keep 4 bits per channel: chunky and banded.
  for (int by = 0; by + 1 < h; by += 2) {
    for (int bx = 0; bx + 1 < w; bx += 2) {
      int r = 0, g = 0, b = 0;
      for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
          const uint16_t v = swap16(px[(by + j) * w + bx + i]);
          r += v >> 11;
          g += (v >> 5) & 63;
          b += v & 31;
        }
      r = (r / 4) & 0x1E;
      g = (g / 4) & 0x3C;
      b = (b / 4) & 0x1E;
      const uint16_t c = swap16((uint16_t)((r << 11) | (g << 5) | b));
      px[by * w + bx] = px[by * w + bx + 1] = px[(by + 1) * w + bx] = px[(by + 1) * w + bx + 1] = c;
    }
  }
}

}  // namespace cinefx
