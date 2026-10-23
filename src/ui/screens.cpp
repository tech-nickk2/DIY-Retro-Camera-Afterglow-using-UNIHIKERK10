#include "screens.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "../gfx/font5x7.h"

using namespace gfx;

namespace ui {

static float clamp01(float p) { return p < 0 ? 0 : p > 1 ? 1 : p; }
static float ease(float p) { return p < .5f ? 2 * p * p : 1 - powf(-2 * p + 2, 2) / 2; }

// ------------------------------------------------------------------ boot

void drawBoot(Canvas& c, float t, const char* statusLine) {
  c.fill(rgb(10, 9, 8));
  for (int i = 0; i < 5; i++) {
    const float p = clamp01((t - .2f - i * .08f) / .5f);
    c.fillRect(0, 66 + i * 6, (int)(ease(p) * W), 6, col::STRIPES[i]);
  }
  const char* word = "AFTERGLOW";
  const int shown = (int)(clamp01((t - .8f) / .9f) * 9);
  char part[10] = {};
  memcpy(part, word, shown);
  c.text(W / 2 - Canvas::textWidth(word, 3) / 2, 112, part, col::CREAM, 3, Align::Left, false);
  if (t > 1.8f) c.text(W / 2, 144, "MODEL K-10  " G_DOT "  FIRMWARE 0.3", col::DIM, 1, Align::Center, false);
  if (t > 2.3f && statusLine) c.text(W / 2, 176, statusLine, col::AMBER, 1, Align::Center, false);
}

// ------------------------------------------------------------------ darkroom

static uint16_t* s_darkBg = nullptr;

static void darkroomBackground(uint16_t* px) {
  // Deep red room with the safelight glowing above the tray.
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const float dx = (x - 160) / 230.f, dy = (y - 70) / 230.f;
      const float g = clamp01(1 - sqrtf(dx * dx + dy * dy)) * .5f;
      px[y * W + x] = rgb((uint8_t)(26 + 174 * g), (uint8_t)(5 + 35 * g), (uint8_t)(4 + 21 * g));
    }
}

void drawDarkroom(Canvas& c, const uint16_t* print, float p, float t) {
  if (!s_darkBg) {
    s_darkBg = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);  // drawn once
    if (s_darkBg) darkroomBackground(s_darkBg);
  }
  if (s_darkBg) memcpy(c.pixels(), s_darkBg, W * H * 2);
  else c.fill(rgb(26, 5, 4));

  c.fillRect(22, 10, 276, 192, rgb(12, 3, 3));  // the tray
  const int px = 40, py = 22, pw = 240, ph = 168;
  // Paper border, already under the red light.
  c.fillRect(px - 6, py - 6, pw + 12, ph + 12, rgb(255, 72, 56));

  // The print comes up darks first: every channel is max(image, L) with L
  // falling from white to black, then lit by the safelight.
  const float e = ease(clamp01(p));
  const int L = (int)(255 * (1 - e));
  const int Lr = L >> 3, Lg = L >> 2, Lb = L >> 3;
  uint16_t* dst = c.pixels();
  for (int y = 0; y < ph; y++) {
    // Liquid in the tray: each 4-line slice sways a pixel or so.
    const int wobble = (int)lroundf(sinf((y & ~3) * .08f + t * 5) * 1.2f);
    const uint16_t* srow = print + (y * H / ph) * W;
    uint16_t* drow = dst + (py + y) * W + px + wobble;
    for (int x = 0; x < pw; x++) {
      const uint16_t v = swap16(srow[x * W / pw]);
      int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
      r = r > Lr ? r : Lr;
      g = g > Lg ? g : Lg;
      b = b > Lb ? b : Lb;
      g = (g * 72) >> 8;  // safelight multiply (255, 72, 56)
      b = (b * 56) >> 8;
      drow[x] = swap16((uint16_t)((r << 11) | (g << 5) | b));
    }
  }

  char label[16] = "DEVELOPING";
  const int dots = 1 + (int)(t / .35f) % 3;
  strncat(label, "...", dots);
  c.text(110, 210, label, rgb(0xFF, 0x9D, 0x8A), 1, Align::Left, false);
  c.fillRect(80, 224, 160, 4, rgb(0x3A, 0x0C, 0x08));
  c.fillRect(80, 224, (int)(160 * clamp01(p)), 4, rgb(0xFF, 0x7A, 0x60));
}

// ------------------------------------------------------------------ review

void drawReview(Canvas& c, const uint16_t* print, float t, int frame, const film::Stock& st) {
  memcpy(c.pixels(), print, W * H * 2);
  const float p = clamp01(t / .45f);
  if (p < 1) {
    // The safelight fades as the room lights come on.
    const int kg = (int)(256 * (72 / 255.f + (1 - 72 / 255.f) * p));
    const int kb = (int)(256 * (56 / 255.f + (1 - 56 / 255.f) * p));
    uint16_t* px = c.pixels();
    for (int i = 0; i < W * H; i++) {
      const uint16_t v = swap16(px[i]);
      const int g = ((((v >> 5) & 63) * kg) >> 8), b = (((v & 31) * kb) >> 8);
      px[i] = swap16((uint16_t)((v & 0xF800) | (g << 5) | b));
    }
  }
  c.dim(0, 0, W, 18, 100);
  char cap[40];
  snprintf(cap, sizeof(cap), "FRAME %02d  " G_DOT "  %s", frame, st.name);
  c.text(8, 6, cap, col::CREAM, 1, Align::Left, false);
  c.text(W - 8, 6, "PRESS " G_RIGHT " GO", col::DIM, 1, Align::Right, false);
}

// ------------------------------------------------------------------ rewind

void drawRewind(Canvas& c, float p, int from, float t) {
  c.fill(rgb(10, 9, 8));
  c.text(W / 2, 30, "REWINDING", col::CREAM, 2, Align::Center, false);
  const int v = (int)lroundf(from * (1 - ease(clamp01(p))));
  char s[4];
  if (v == 0) snprintf(s, sizeof(s), "S");
  else snprintf(s, sizeof(s), "%02d", v);
  const int tw = Canvas::seg7Width(s, 26, 5, 6);
  c.seg7(W / 2 - tw / 2, 78, s, 26, 46, 5, 6, col::WHITE);
  // The rewind crank turning.
  c.circle(160, 166, 12, col::AMBER);
  c.circle(160, 166, 11, col::AMBER);
  const float a = -t * 16;
  c.line(160, 166, 160 + (int)(cosf(a) * 12), 166 + (int)(sinf(a) * 12), col::AMBER);
  c.fillRect(80, 200, 160, 4, rgb(0x2A, 0x24, 0x20));
  c.fillRect(80, 200, (int)(160 * clamp01(p)), 4, col::AMBER);
}

// ------------------------------------------------------------------ instant print

void drawInstantPrint(Canvas& c, const uint16_t* print, int fy, float fog, const char* caption) {
  const int fx = 75, fw = 170, fh = 196;
  c.fillRect(fx + 3, fy + 4, fw, fh, rgb(5, 4, 4));
  c.fillRect(fx, fy, fw, fh, rgb(0xF3, 0xF0, 0xE8));
  // The haze an instant print develops out of: rgb(122, 138, 142).
  const int k = (int)(256 * clamp01(fog)), keep = 256 - k;
  const int fr = (122 >> 3) * k, fg = (138 >> 2) * k, fb = (142 >> 3) * k;
  for (int y = 0; y < 150; y++) {
    const int dy = fy + 10 + y;
    if (dy < 0 || dy >= H) continue;
    const uint16_t* src = print + (y * 240 / 150) * W + 40;
    uint16_t* d = c.pixels() + dy * W + fx + 10;
    for (int x = 0; x < 150; x++) {
      const uint16_t p = src[x * 240 / 150];
      if (!k) { d[x] = p; continue; }
      const uint16_t v = swap16(p);
      const int r = ((v >> 11) * keep + fr) >> 8, g = (((v >> 5) & 63) * keep + fg) >> 8, b = ((v & 31) * keep + fb) >> 8;
      d[x] = swap16((uint16_t)((r << 11) | (g << 5) | b));
    }
  }
  if (caption) c.text(fx + 16, fy + 172, caption, rgb(0x2B, 0x3A, 0x67), 1, Align::Left, false);
}

void drawInstantDevelop(Canvas& c, const uint16_t* print, float t, float ejectS, float developS) {
  c.fill(rgb(0x14, 0x12, 0x10));
  c.fillRect(60, 0, 200, 4, rgb(5, 4, 4));  // the slot it comes out of
  const int fy = t < ejectS ? -196 + (int)(ease(t / ejectS) * 218) : 22;
  const float fog = t < ejectS ? 1 : 1 - ease(clamp01((t - ejectS) / developS));
  drawInstantPrint(c, print, fy, fog, nullptr);
  if (t > ejectS) c.text(W / 2, 226, "DEVELOPING", col::DIM, 1, Align::Center, false);
}

// ------------------------------------------------------------------ mode switch

void drawModeSwitch(Canvas& c, float t, const char* label, const char* sub) {
  // Close 0.35 s, hold, open 0.38 s.
  float r;
  if (t < .35f) r = 210 * (1 - ease(t / .35f));
  else if (t < .52f) r = 0;
  else r = 210 * ease(clamp01((t - .52f) / .38f));
  const int r2 = (int)(r * r);
  uint16_t* px = c.pixels();
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const int dx = x - 160, dy = y - 120;
      if (dx * dx + dy * dy > r2) px[y * W + x] = col::BLACK;
    }
  if (t > .25f && t < .76f) {
    c.text(W / 2, 100, label, col::AMBER, 3, Align::Center, false);
    c.text(W / 2, 130, sub, col::CREAM, 1, Align::Center, false);
  }
}

// ------------------------------------------------------------------ canister and loading

void drawCanister(Canvas& c, int x, int y, int s, const film::Stock& st) {
  auto R = [&](float ux, float uy, float uw, float uh, uint16_t k) {
    c.fillRect(x + (int)lroundf(ux * s), y + (int)lroundf(uy * s), (int)lroundf(uw * s), (int)lroundf(uh * s), k);
  };
  if (st.pack) {
    R(0, 6, 24, 20, rgb(0x2B, 0x2A, 0x2C));
    R(0, 6, 24, 2, rgb(0x3D, 0x3C, 0x3F));
    for (int i = 0; i < 5; i++) R(2 + i, 8, 1, 16, col::STRIPES[i]);
    R(9, 10, 13, 12, rgb(0xE9, 0xE6, 0xDF));
    R(10, 11, 11, 9, rgb(0x6E, 0x7F, 0x86));
    return;
  }
  const uint16_t grey = rgb(0xA8, 0xA8, 0xA8), leader = rgb(0x5A, 0x3A, 0x1C), hole = rgb(0x2A, 0x19, 0x0B);
  R(5, 0, 6, 2, rgb(0x6B, 0x6B, 0x6B));
  R(1, 2, 14, 2, grey);
  R(0, 4, 16, 20, st.c1);
  R(0, 10, 16, 8, st.c2);
  R(1, 24, 14, 2, grey);
  R(16, 8, 8, 12, leader);
  for (int k = 0; k < 3; k++) {
    R(17 + k * 2.5f, 9, 1, 1, hole);
    R(17 + k * 2.5f, 18, 1, 1, hole);
  }
  if (s >= 3) {
    char iso[8];
    snprintf(iso, sizeof(iso), "%d", st.iso);
    c.text(x + 8 * s, y + (int)(11.5f * s), iso, st.c1, 1, Align::Center, false);
  }
}

void drawLoading(Canvas& c, float t, const film::Stock& st) {
  c.fill(rgb(14, 12, 11));
  c.fillRect(0, 62, W, 108, rgb(0x1C, 0x19, 0x17));
  c.hline(0, 70, W, rgb(0x2C, 0x28, 0x24));
  c.hline(0, 160, W, rgb(0x2C, 0x28, 0x24));
  c.text(W / 2, 22, st.pack ? "LOADING PACK" : "LOADING FILM", col::CREAM, 2, Align::Center, false);
  c.text(W / 2, 44, st.name, col::AMBER, 1, Align::Center, false);
  const int cx = -80 + (int)(ease(clamp01(t / .6f)) * 110);
  if (!st.pack) {
    // The leader pulled across to the take-up spool.
    const int len = (int)(ease(clamp01((t - .7f) / .9f)) * 180), lx0 = cx + 48, ly = 100;
    c.fillRect(lx0, ly, len, 36, rgb(0x5A, 0x3A, 0x1C));
    for (int x = 4; x < len - 4; x += 9) {
      c.fillRect(lx0 + x, ly + 3, 5, 4, rgb(0x1C, 0x19, 0x17));
      c.fillRect(lx0 + x, ly + 29, 5, 4, rgb(0x1C, 0x19, 0x17));
    }
    c.fillCircle(276, 118, 26, rgb(0x9E, 0xA3, 0xA9));
    c.fillCircle(276, 118, 8, rgb(0x4A, 0x4E, 0x53));
    const float rot = t > 1.5f ? (t - 1.5f) * 8 : 0;
    for (int k = 0; k < 3; k++) {
      const float a = rot + k * 2.094f;
      for (int w = -1; w <= 1; w++)
        c.line(276 + w, 118, 276 + w + (int)(cosf(a) * 22), 118 + (int)(sinf(a) * 22), rgb(0x4A, 0x4E, 0x53));
    }
  }
  drawCanister(c, cx, 76, 3, st);
  char lab[4];
  if (st.pack) snprintf(lab, sizeof(lab), "%d", st.pack);
  else snprintf(lab, sizeof(lab), "%s", t < 1.8f ? "S" : "1");
  const int tw = Canvas::seg7Width(lab, 11, 3, 3);
  c.seg7(W / 2 - tw / 2, 184, lab, 11, 22, 3, 3, t < 1.8f && !st.pack ? col::DIM : col::WHITE);
  c.text(W / 2, 214, st.pack ? "SHOTS" : "FRAME", col::DIM, 1, Align::Center, false);
}

}  // namespace ui
