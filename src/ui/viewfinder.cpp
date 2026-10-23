#include "viewfinder.h"
#include <math.h>
#include <stdio.h>
#include "../gfx/font5x7.h"

using namespace gfx;

namespace ui {

static const struct { float t; const char* label; } SPEEDS[] = {
  {1 / 1000.f, "1000"}, {1 / 500.f, "500"}, {1 / 250.f, "250"}, {1 / 125.f, "125"},
  {1 / 60.f, "60"},     {1 / 30.f, "30"},   {1 / 15.f, "15"},   {1 / 8.f, "8"},
};

Exposure meter(float lux, int iso) {
  if (lux < 0) return {0, "--", false};
  if (lux < 0.5f) lux = 0.5f;
  const float ev = log2f(lux / 2.5f) + log2f(iso / 100.f);
  const float tNeed = 7.84f / powf(2, ev);  // f/2.8
  int best = 0;
  for (int i = 1; i < 8; i++)
    if (fabsf(log2f(SPEEDS[i].t / tNeed)) < fabsf(log2f(SPEEDS[best].t / tNeed))) best = i;
  return {log2f(SPEEDS[best].t / tNeed), SPEEDS[best].label, SPEEDS[best].t >= 1 / 30.f};
}

void miniCanister(Canvas& c, int x, int y, const film::Stock& st) {
  if (st.pack) {
    c.fillRect(x, y + 1, 8, 8, rgb(0xE9, 0xE6, 0xDF));
    for (int i = 0; i < 5; i++) c.fillRect(x + 1, y + 2 + i, 6, 1, col::STRIPES[i]);
    return;
  }
  c.fillRect(x + 1, y, 5, 1, rgb(0xA8, 0xA8, 0xA8));
  c.fillRect(x + 1, y + 9, 5, 1, rgb(0xA8, 0xA8, 0xA8));
  c.fillRect(x, y + 1, 7, 8, st.c1);
  c.fillRect(x, y + 4, 7, 3, st.c2);
}

static void brackets(Canvas& c, bool square) {
  const int x0 = square ? 50 : 22, y0 = 24, x1 = W - x0, y1 = 202, L = 12;
  const uint16_t k = rgb(230, 230, 230);
  c.hline(x0, y0, L, k); c.vline(x0, y0, L, k);
  c.hline(x1 - L, y0, L, k); c.vline(x1 - 1, y0, L, k);
  c.hline(x0, y1, L, k); c.vline(x0, y1 - L + 1, L, k);
  c.hline(x1 - L, y1, L, k); c.vline(x1 - 1, y1 - L + 1, L, k);
}

static void centre(Canvas& c, const ViewState& s) {
  const int cx = 160, cy = 112;
  c.circle(cx, cy, 14, rgb(200, 200, 200));
  c.hline(cx - 13, cy, 27, rgb(140, 140, 140));
  // Fixed ticks mark the frame's horizontal; the line follows the real horizon.
  c.vline(cx - 50, cy - 3, 7, rgb(170, 170, 170));
  c.vline(cx + 50, cy - 3, 7, rgb(170, 170, 170));
  if (!s.levelValid) return;
  const bool level = fabsf(s.tiltDeg) < 1.5f;
  const uint16_t k = level ? col::GREEN : col::AMBER;
  const float a = -s.tiltDeg * (float)M_PI / 180.f, ca = cosf(a), sa = sinf(a);
  for (int side = -1; side <= 1; side += 2) {
    const int r0 = 20 * side, r1 = 46 * side;
    c.line(cx + (int)(ca * r0), cy + (int)(sa * r0), cx + (int)(ca * r1), cy + (int)(sa * r1), k);
  }
}

static void counter(Canvas& c, const ViewState& s) {
  const int cx = 160, slot = 30;
  c.setClip(106, 214, 108, 26);
  for (int k = -3; k <= 3; k++) {
    const int n = s.frame + k;
    char buf[12];
    if (n < 0 || n > s.rollLen) continue;
    if (n == 0 && !s.countDown) snprintf(buf, sizeof(buf), "S");
    else snprintf(buf, sizeof(buf), "%02d", n);
    const float pos = k - s.counterOffset;
    const int x = cx + (int)lroundf(pos * slot);
    const bool inWindow = fabsf(pos) < .5f;
    const bool nearEdge = fabsf(pos) > 1.5f;
    c.text(x, 219, buf, inWindow ? col::WHITE : nearEdge ? rgb(0x3A, 0x36, 0x32) : col::DARKDIM, 2, Align::Center, false);
  }
  c.resetClip();
  c.rect(cx - 16, 216, 32, 22, col::AMBER);
  c.fillRect(cx - 2, 214, 5, 1, col::AMBER);
  c.fillRect(cx - 1, 215, 3, 1, col::AMBER);
}

static void lightMeter(Canvas& c, const ViewState& s) {
  const int iso = s.noFilm ? 200 : s.stock->iso;
  const Exposure e = meter(s.lux, iso);
  const int mx = 24, my = 238, mr = 17;
  const float P = (float)M_PI;
  c.arc(mx, my, mr, P * 1.12f, P * 1.30f, col::RED, 2);
  c.arc(mx, my, mr, P * 1.30f, P * 1.70f, rgb(0xD9, 0xD2, 0xC4), 2);
  c.arc(mx, my, mr, P * 1.70f, P * 1.88f, col::RED, 2);
  float err = e.err < -3 ? -3 : e.err > 3 ? 3 : e.err;
  const float ang = P * 1.5f + (err / 3) * P * .38f;
  c.line(mx, my, mx + (int)(cosf(ang) * (mr - 2)), my + (int)(sinf(ang) * (mr - 2)), col::WHITE);
  const bool blink = (s.nowMs / 350) % 2 == 0;
  if (!e.shake || blink) c.text(48, 219, e.label, e.shake ? col::RED : col::WHITE, 1, Align::Left, false);
  char iso_[12];
  if (e.shake) snprintf(iso_, sizeof(iso_), "SHAKE");
  else if (s.noFilm) snprintf(iso_, sizeof(iso_), "ISO--");
  else snprintf(iso_, sizeof(iso_), "ISO%d", iso);
  c.text(48, 229, iso_, e.shake ? col::RED : col::DIM, 1, Align::Left, false);
}

static void centreBox(Canvas& c, const char* l1, const char* l2) {
  c.fillRect(56, 86, 208, 54, rgb(8, 7, 6));
  c.hline(56, 86, 208, col::AMBER);
  c.hline(56, 139, 208, col::AMBER);
  c.text(160, 96, l1, col::CREAM, 2, Align::Center, false);
  c.text(160, 122, l2, col::AMBER, 1, Align::Center, false);
}

static void sprockets(Canvas& c, float p) {
  // A strip of film slides past while winding.
  if (p <= 0 || p >= 1) return;
  const float e = p < .5f ? 2 * p * p : 1 - powf(-2 * p + 2, 2) / 2;
  const int off = (int)(e * 60) % 12;
  c.fillRect(0, 204, W, 9, rgb(0x3A, 0x24, 0x12));
  for (int x = -12; x < W + 12; x += 12) c.fillRect(x - off, 206, 6, 5, rgb(0x0C, 0x08, 0x06));
}

void drawViewfinder(Canvas& c, const ViewState& s) {
  const bool square = !s.noFilm && s.stock->pack;
  if (square) {  // instant film is square: shade what will not be on the print
    c.dim(0, 0, 40, 214, 128);
    c.dim(280, 0, 40, 214, 128);
  }
  brackets(c, square);
  centre(c, s);

  if (s.noFilm) c.text(10, 8, "NO FILM", col::RED, 1);
  else {
    miniCanister(c, 9, 6, *s.stock);
    c.text(22, 8, s.stock->name, col::WHITE, 1);
  }
  if (s.flashOn && (!s.flashCharging || (s.nowMs / 250) % 2 == 0))
    c.text(W - 12, 8, G_FLASH, s.flashCharging ? col::DIM : rgb(0xFF, 0xE3, 0x6B), 1, Align::Right);

  c.dim(0, 214, W, 26, 30);  // bottom strip
  lightMeter(c, s);
  counter(c, s);
  if (square) c.text(312, 224, "INSTANT", col::DIM, 1, Align::Right, false);
  else if (s.dateOn) {
    char d[16];
    film::dateText(d, sizeof(d));
    const int tw = Canvas::seg7Width(d, 6, 2, 2);
    c.seg7(312 - tw, 221, d, 6, 12, 2, 2, col::DATE);
  } else {
    c.text(312, 224, "DATE OFF", col::DARKDIM, 1, Align::Right, false);
  }
  sprockets(c, s.windP);

  if (s.noFilm) centreBox(c, "NO FILM", "PRESS " G_RIGHT " LOAD A ROLL");
  else if (s.rollDone) centreBox(c, square ? "PACK EMPTY" : "END OF ROLL", square ? "PRESS " G_RIGHT " NEW PACK" : "TURN " G_LEFT " TO REWIND");
  else if (s.needWind && s.windP <= 0 && (s.nowMs / 450) % 2 == 0) {
    const char* m = "TURN " G_RIGHT " WIND";
    const int w = Canvas::textWidth(m, 2) + 12;
    c.fillRect(160 - w / 2, 184, w, 22, rgb(8, 7, 6));
    c.text(160, 188, m, col::AMBER, 2, Align::Center, false);
  }
}

}  // namespace ui
