#include "menus.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cine.h"
#include "screens.h"
#include "../gfx/font5x7.h"

using namespace gfx;

namespace ui {

static const char* MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void hint(Canvas& c, const char* s) { c.text(W / 2, 228, s, col::DIM, 1, Align::Center, false); }

static void stripesTitle(Canvas& c, const char* title) {
  for (int i = 0; i < 5; i++) c.fillRect(12 + i * 3, 8, 3, 15, col::STRIPES[i]);
  c.text(34, 9, title, col::CREAM, 2, Align::Left, false);
}

// ------------------------------------------------------------------ settings

void drawMenu(Canvas& c, const MenuRow* rows, int n, int sel) {
  c.dim(0, 0, W, H, 51);
  stripesTitle(c, "SETTINGS");
  c.text(W - 12, 13, "PHOTO", col::DIM, 1, Align::Right, false);
  constexpr int VISIBLE = 9, RH = 20, Y0 = 32;
  int first = sel - VISIBLE / 2;
  if (first > n - VISIBLE) first = n - VISIBLE;
  if (first < 0) first = 0;
  for (int r = 0; r < VISIBLE && first + r < n; r++) {
    const int i = first + r, y = Y0 + r * RH;
    const bool on = i == sel;
    const uint16_t k = on ? rgb(0x1A, 0x14, 0x10) : col::CREAM;
    if (on) c.fillRect(8, y - 3, 304, 19, col::AMBER);
    c.text(14, y, rows[i].label, k, 2, Align::Left, false);
    if (rows[i].value[0]) c.text(306, y, rows[i].value, on ? k : rgb(0xC8, 0xBF, 0xAE), 2, Align::Right, false);
  }
  // Small triangles when there are more rows above or below.
  for (int k = 0; k < 3; k++) {
    if (first > 0) c.hline(W - 9 - k, 25 + k, 1 + 2 * k, col::DIM);
    if (first + VISIBLE < n) c.hline(W - 9 - k, 215 - k, 1 + 2 * k, col::DIM);
  }
  hint(c, "TURN SCROLL  " G_DOT "  PRESS CHANGE  " G_DOT "  HOLD CLOSE");
}

// ------------------------------------------------------------------ film shelf

void drawShelf(Canvas& c, int sel, float off, int loaded, bool forced, const uint16_t* preview, int rollLen) {
  c.fill(rgb(0x16, 0x11, 0x0D));
  c.text(12, 10, forced ? "LOAD A ROLL" : "FILM SHELF", col::CREAM, 2, Align::Left, false);
  c.fillRect(0, 150, 196, 6, rgb(0x3B, 0x2A, 0x1C));
  c.hline(0, 150, 196, rgb(0x5A, 0x42, 0x30));
  c.setClip(0, 30, 196, 128);
  for (int i = 0; i < film::STOCK_COUNT; i++) {
    const float d = i - off;
    if (fabsf(d) > 2.6f) continue;
    const bool centre = fabsf(d) < .5f;
    const int s = centre ? 3 : 2;
    drawCanister(c, 98 + (int)(d * 64) - 12 * s, 150 - 26 * s, s, film::STOCKS[i]);
    if (!centre) c.dim(98 + (int)(d * 64) - 12 * s, 150 - 26 * s, 24 * s + 2, 26 * s, 140);
    if (centre && i == loaded) c.text(98, 36, "LOADED", col::GREEN, 1, Align::Center, false);
  }
  c.resetClip();
  c.fillRect(199, 37, 114, 88, col::BLACK);
  for (int y = 0; y < 84; y++) memcpy(c.pixels() + (38 + y) * W + 200, preview + y * 112, 112 * 2);
  c.text(256, 130, "PREVIEW", col::DIM, 1, Align::Center, false);
  const film::Stock& st = film::STOCKS[sel];
  c.text(12, 166, st.name, col::AMBER, 2, Align::Left, false);
  char l[40];
  if (st.pack) snprintf(l, sizeof(l), "PACK OF %d  " G_DOT "  ISO %d", st.pack, st.iso);
  else snprintf(l, sizeof(l), "ISO %d  " G_DOT "  %d EXP", st.iso, rollLen);
  c.text(12, 187, l, rgb(0xE8, 0xE2, 0xD6), 1, Align::Left, false);
  c.text(12, 199, st.desc, col::DIM, 1, Align::Left, false);
  hint(c, "TURN BROWSE  " G_DOT "  PRESS LOAD  " G_DOT "  HOLD CANCEL");
}

// ------------------------------------------------------------------ album

static void itemDate(const gallery::Item& it, char* out, int len) {
  time_t t = it.time;
  struct tm tm;
  localtime_r(&t, &tm);
  snprintf(out, len, "%s %d", MONTHS[tm.tm_mon % 12], tm.tm_mday);
}

static const char* stockName(uint8_t i) { return i < film::STOCK_COUNT ? film::STOCKS[i].name : "FILM"; }
static const char* cineName(uint8_t i) { return cineLookName(i); }

void drawAlbum(Canvas& c, const gallery::Item* items, int n, int sel, float off, ThumbFn thumb) {
  c.fill(rgb(0x0D, 0x0B, 0x0A));
  c.text(12, 10, "ALBUM", col::AMBER, 2, Align::Left, false);
  char l[48];
  snprintf(l, sizeof(l), "%d %s", n, n == 1 ? "PHOTO" : "PHOTOS");
  c.text(W - 12, 14, l, col::DIM, 1, Align::Right, false);
  if (!n) {
    c.text(W / 2, 104, "NO PHOTOS YET", col::CREAM, 2, Align::Center, false);
    c.text(W / 2, 128, "GO SHOOT SOMETHING", col::DIM, 1, Align::Center, false);
    hint(c, "HOLD BACK");
    return;
  }
  const int sy = 58, shh = 120, slot = 112;
  c.fillRect(0, sy, W, shh, rgb(0x1D, 0x14, 0x0D));
  const int holeOff = ((int)(off * slot) % 12 + 12) % 12;
  for (int x = -12; x < W + 12; x += 12) {
    c.fillRect(x - holeOff, sy + 4, 6, 5, rgb(0x0D, 0x0B, 0x0A));
    c.fillRect(x - holeOff, sy + shh - 9, 6, 5, rgb(0x0D, 0x0B, 0x0A));
  }
  const uint16_t edge = rgb(0xE8, 0x8A, 0x2E);
  for (int i = 0; i < n; i++) {
    const int cx = 160 + (int)lroundf((i - off) * slot);
    if (cx < -70 || cx > W + 70) continue;
    const int cy = sy + shh / 2;
    c.fillRect(cx - 51, cy - 39, 102, 78, rgb(0x0A, 0x07, 0x06));
    const uint16_t* th = thumb(items[i].num);
    if (th) {
      // Clip by hand: frames slide off both edges of the screen.
      for (int y = 0; y < gallery::THUMB_H; y++) {
        const int dy = cy - 37 + y;
        for (int x = 0; x < gallery::THUMB_W; x++) {
          const int dx = cx - 50 + x;
          if (dx >= 0 && dx < W) c.pixels()[dy * W + dx] = th[y * gallery::THUMB_W + x];
        }
      }
    }
    const bool clip = items[i].kind == gallery::CLIP;
    if (clip) {
      c.dim(cx - 50, cy + 26, 100, 12, 90);
      snprintf(l, sizeof(l), G_RIGHT " %d.%dS", items[i].tenths / 10, items[i].tenths % 10);
      c.text(cx - 46, cy + 28, l, col::WHITE, 1, Align::Left, false);
    }
    c.text(cx - 50, sy + 11, clip ? cineName(items[i].stock) : stockName(items[i].stock), edge, 1, Align::Left, false);
    if (clip) snprintf(l, sizeof(l), "CINE");
    else snprintf(l, sizeof(l), G_RIGHT "%d  " G_RIGHT "%dA", items[i].frame, items[i].frame);
    c.text(cx + 50, sy + shh - 19, l, edge, 1, Align::Right, false);
    if (i == sel) c.rect(cx - 53, cy - 41, 106, 82, col::AMBER);
  }
  const gallery::Item& it = items[sel];
  char d[12];
  itemDate(it, d, sizeof(d));
  if (it.kind == gallery::CLIP)
    snprintf(l, sizeof(l), "%s  " G_DOT "  %d.%d S  " G_DOT "  %s", cineName(it.stock), it.tenths / 10, it.tenths % 10, d);
  else snprintf(l, sizeof(l), "FRAME %02d  " G_DOT "  %s  " G_DOT "  %s", it.frame, stockName(it.stock), d);
  c.text(W / 2, 192, l, col::CREAM, 1, Align::Center, false);
  snprintf(l, sizeof(l), "%d / %d", sel + 1, n);
  c.text(W / 2, 206, l, col::DIM, 1, Align::Center, false);
  hint(c, it.kind == gallery::CLIP ? "PRESS PLAY  " G_DOT "  HOLD BACK  " G_DOT "  PUSH+TURN DELETE"
                                    : "PRESS OPEN  " G_DOT "  HOLD BACK  " G_DOT "  PUSH+TURN DELETE");
}

void drawPhotoView(Canvas& c, const uint16_t* preview, const gallery::Item& it, int idx, int n) {
  char l[48], d[12];
  itemDate(it, d, sizeof(d));
  if (it.kind == gallery::INSTANT) {
    // An instant print on the table, with a pen caption.
    c.fill(rgb(0x14, 0x12, 0x10));
    snprintf(l, sizeof(l), "%s  " G_DOT "  #%02d", d, it.frame);
    drawInstantPrint(c, preview, 20, 0, l);
    c.text(W - 8, 228, "TURN NEXT  " G_DOT "  PRESS DELETE  " G_DOT "  HOLD BACK", col::DIM, 1, Align::Right, false);
    return;
  }
  memcpy(c.pixels(), preview, W * H * 2);
  c.dim(0, 0, W, 18, 100);
  snprintf(l, sizeof(l), "%d/%d  " G_DOT "  FRAME %02d  " G_DOT "  %s", idx + 1, n, it.frame, stockName(it.stock));
  c.text(8, 6, l, col::CREAM, 1, Align::Left, false);
  c.text(W - 8, 6, "TURN NEXT", col::DIM, 1, Align::Right, false);
  c.dim(0, 222, W, 18, 100);
  c.text(W / 2, 227, "PRESS TO DELETE  " G_DOT "  HOLD BACK", col::DIM, 1, Align::Center, false);
}

void drawConfirm(Canvas& c, const char* l1, const char* l2, bool deleteChosen) {
  c.dim(0, 0, W, H, 70);
  c.fillRect(46, 70, 228, 96, rgb(12, 10, 8));
  c.hline(46, 70, 228, col::RED);
  c.hline(46, 165, 228, col::RED);
  c.text(W / 2, 80, l1, col::CREAM, 2, Align::Center, false);
  c.text(W / 2, 100, l2, col::DIM, 1, Align::Center, false);
  // Two choices; the knob turns between them. KEEP is the safe default.
  const char* opts[2] = {"KEEP", "DELETE"};
  for (int i = 0; i < 2; i++) {
    const bool on = (i == 1) == deleteChosen;
    const int x = i == 0 ? 72 : 172;
    if (on) c.fillRect(x, 116, 76, 20, i == 1 ? col::RED : col::AMBER);
    else c.rect(x, 116, 76, 20, col::DARKDIM);
    c.text(x + 38, 120, opts[i], on ? rgb(0x1A, 0x14, 0x10) : col::DIM, 2, Align::Center, false);
  }
  c.text(W / 2, 148, "TURN CHOOSE  " G_DOT "  PRESS OK", col::DIM, 1, Align::Center, false);
}

// ------------------------------------------------------------------ date back

void drawDateSet(Canvas& c, const struct tm& t, int field, uint32_t nowMs) {
  c.fill(rgb(10, 9, 8));
  stripesTitle(c, "SET DATE");
  c.text(W / 2, 40, "LIKE THE DATE BACK ON AN OLD CAMERA", col::DIM, 1, Align::Center, false);
  char yr[12], mo[12], dy[12], hr[12], mi[12];
  snprintf(yr, sizeof(yr), "%04d", t.tm_year + 1900);
  snprintf(mo, sizeof(mo), "%02d", t.tm_mon + 1);
  snprintf(dy, sizeof(dy), "%02d", t.tm_mday);
  snprintf(hr, sizeof(hr), "%02d", t.tm_hour);
  snprintf(mi, sizeof(mi), "%02d", t.tm_min);
  const char* parts[5] = {yr, mo, dy, hr, mi};
  const char* names[5] = {"YEAR", "MONTH", "DAY", "HOUR", "MIN"};
  // Two rows of orange LED digits: date, then time.
  const int dw = 14, dh = 26, dt = 3, gap = 5;
  const int xs[5] = {40, 150, 220, 105, 185}, ys[5] = {70, 70, 70, 140, 140};
  const bool blink = (nowMs / 400) % 2 == 0;
  for (int i = 0; i < 5; i++) {
    const bool on = i == field;
    const uint16_t k = on ? col::DATE : rgb(0x7A, 0x3A, 0x10);
    if (!on || blink) c.seg7(xs[i], ys[i], parts[i], dw, dh, dt, gap, k);
    const int w = Canvas::seg7Width(parts[i], dw, dt, gap);
    c.text(xs[i] + w / 2, ys[i] + dh + 8, names[i], on ? col::AMBER : col::DARKDIM, 1, Align::Center, false);
    if (on) c.hline(xs[i], ys[i] + dh + 4, w, col::AMBER);
  }
  c.text(170, 140 + 8, ":", col::DATE, 2, Align::Left, false);
  hint(c, "TURN CHANGE  " G_DOT "  PRESS NEXT FIELD  " G_DOT "  HOLD SAVE");
}

// ------------------------------------------------------------------ self-timer

void drawTimer(Canvas& c, int remainingMs) {
  char s[12];
  snprintf(s, sizeof(s), "%d", (remainingMs + 999) / 1000);
  const int tw = Canvas::seg7Width(s, 26, 5, 6);
  const uint16_t k = remainingMs < 2000 ? col::RED : col::WHITE;
  c.seg7(W / 2 - tw / 2 + 2, 90, s, 26, 46, 5, 6, col::BLACK);
  c.seg7(W / 2 - tw / 2, 88, s, 26, 46, 5, 6, k);
  c.text(W / 2, 146, "SELF TIMER", col::WHITE, 1, Align::Center);
  c.text(W / 2, 200, "PRESS " G_RIGHT " CANCEL", col::DIM, 1, Align::Center);
}

}  // namespace ui
