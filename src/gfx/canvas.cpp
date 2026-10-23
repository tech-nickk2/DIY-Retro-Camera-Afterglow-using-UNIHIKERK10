#include "canvas.h"
#include <math.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "font5x7.h"

namespace gfx {

bool Canvas::begin() {
  px_ = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  w_ = W;
  h_ = H;
  resetClip();
  if (px_) fill(col::BLACK);
  return px_ != nullptr;
}

void Canvas::wrap(uint16_t* px, int w, int h) {
  px_ = px;
  w_ = w;
  h_ = h;
  resetClip();
}

void Canvas::setClip(int x, int y, int w, int h) {
  cx0_ = x < 0 ? 0 : x;
  cy0_ = y < 0 ? 0 : y;
  cx1_ = x + w > w_ ? w_ : x + w;
  cy1_ = y + h > h_ ? h_ : y + h;
}

void Canvas::resetClip() {
  cx0_ = cy0_ = 0;
  cx1_ = w_;
  cy1_ = h_;
}

void Canvas::fill(uint16_t c) {
  uint32_t two = ((uint32_t)c << 16) | c;
  uint32_t* p = (uint32_t*)px_;
  for (int i = 0; i < w_ * h_ / 2; i++) p[i] = two;
}

void Canvas::fillRect(int x, int y, int w, int h, uint16_t c) {
  if (x < cx0_) { w -= cx0_ - x; x = cx0_; }
  if (y < cy0_) { h -= cy0_ - y; y = cy0_; }
  if (x + w > cx1_) w = cx1_ - x;
  if (y + h > cy1_) h = cy1_ - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint16_t* row = px_ + (y + j) * w_ + x;
    for (int i = 0; i < w; i++) row[i] = c;
  }
}

void Canvas::rect(int x, int y, int w, int h, uint16_t c) {
  hline(x, y, w, c);
  hline(x, y + h - 1, w, c);
  vline(x, y, h, c);
  vline(x + w - 1, y, h, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::circle(int cx, int cy, int r, uint16_t c) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    pixel(cx + x, cy + y, c); pixel(cx + y, cy + x, c); pixel(cx - y, cy + x, c); pixel(cx - x, cy + y, c);
    pixel(cx - x, cy - y, c); pixel(cx - y, cy - x, c); pixel(cx + y, cy - x, c); pixel(cx + x, cy - y, c);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

void Canvas::fillCircle(int cx, int cy, int r, uint16_t c) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
    hline(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}

void Canvas::arc(int cx, int cy, int r, float a0, float a1, uint16_t c, int thickness) {
  float step = 0.7f / (r + thickness);
  for (float a = a0; a <= a1; a += step) {
    float ca = cosf(a), sa = sinf(a);
    for (int t = 0; t < thickness; t++) pixel(cx + (int)lroundf(ca * (r - t)), cy + (int)lroundf(sa * (r - t)), c);
  }
}

void Canvas::dim(int x, int y, int w, int h, int keep) {
  if (x < cx0_) { w -= cx0_ - x; x = cx0_; }
  if (y < cy0_) { h -= cy0_ - y; y = cy0_; }
  if (x + w > cx1_) w = cx1_ - x;
  if (y + h > cy1_) h = cy1_ - y;
  if (w <= 0 || h <= 0) return;
  for (int j = 0; j < h; j++) {
    uint16_t* row = px_ + (y + j) * w_ + x;
    for (int i = 0; i < w; i++) {
      uint16_t v = swap16(row[i]);
      uint32_t r = ((v >> 11) * keep) >> 8, g = (((v >> 5) & 63) * keep) >> 8, b = ((v & 31) * keep) >> 8;
      row[i] = swap16((uint16_t)((r << 11) | (g << 5) | b));
    }
  }
}

static inline const uint8_t* glyphFor(uint8_t ch) {
  if (ch >= 'a' && ch <= 'z') ch -= 32;
  if (ch < font5x7::FIRST || ch > font5x7::LAST) ch = '?';
  return font5x7::GLYPHS[ch - font5x7::FIRST];
}

void Canvas::glyphs(int x, int y, const char* s, uint16_t c, int scale) {
  for (; *s; s++, x += 6 * scale) {
    const uint8_t* g = glyphFor((uint8_t)*s);
    for (int r = 0; r < 7; r++)
      for (int k = 0; k < 5; k++)
        if (g[r] & (0x10 >> k)) fillRect(x + k * scale, y + r * scale, scale, scale, c);
  }
}

int Canvas::textWidth(const char* s, int scale) {
  int n = 0;
  while (s[n]) n++;
  return n ? n * 6 * scale - scale : 0;
}

int Canvas::text(int x, int y, const char* s, uint16_t c, int scale, Align a, bool shadow) {
  int w = textWidth(s, scale);
  if (a == Align::Center) x -= w / 2;
  else if (a == Align::Right) x -= w;
  if (shadow) glyphs(x + scale, y + scale, s, col::BLACK, scale);
  glyphs(x, y, s, c, scale);
  return w;
}

static const char* segsFor(char ch) {
  switch (ch) {
    case '0': return "abcdef"; case '1': return "bc"; case '2': return "abged"; case '3': return "abgcd";
    case '4': return "fgbc"; case '5': case 'S': return "afgcd"; case '6': return "afgedc"; case '7': return "abc";
    case '8': return "abcdefg"; case '9': return "abcdfg"; case '-': return "g"; case 'E': return "afged";
    default: return "";
  }
}

int Canvas::seg7Width(const char* s, int w, int t, int gap) {
  int x = 0;
  for (; *s; s++) x += (*s == '\'' || *s == ':') ? t + gap : w + gap;
  return x ? x - gap : 0;
}

int Canvas::seg7(int x, int y, const char* s, int w, int h, int t, int gap, uint16_t c) {
  int hh = (h - 3 * t) / 2;
  for (; *s; s++) {
    if (*s == '\'') { fillRect(x, y, t, h * 28 / 100, c); x += t + gap; continue; }
    if (*s == ':') { fillRect(x, y + h * 28 / 100, t, t, c); fillRect(x, y + h * 66 / 100, t, t, c); x += t + gap; continue; }
    for (const char* g = segsFor(*s); *g; g++) {
      switch (*g) {
        case 'a': fillRect(x + t, y, w - 2 * t, t, c); break;
        case 'b': fillRect(x + w - t, y + t, t, hh, c); break;
        case 'c': fillRect(x + w - t, y + 2 * t + hh, t, hh, c); break;
        case 'd': fillRect(x + t, y + 2 * t + 2 * hh, w - 2 * t, t, c); break;
        case 'e': fillRect(x, y + 2 * t + hh, t, hh, c); break;
        case 'f': fillRect(x, y + t, t, hh, c); break;
        case 'g': fillRect(x + t, y + t + hh, w - 2 * t, t, c); break;
      }
    }
    x += w + gap;
  }
  return x;
}

void scaleNearest(const uint16_t* src, int sw, int sh, uint16_t* dst, int dw, int dh) {
  for (int y = 0; y < dh; y++) {
    const uint16_t* row = src + (size_t)(y * sh / dh) * sw;
    uint16_t* d = dst + y * dw;
    for (int x = 0; x < dw; x++) d[x] = row[x * sw / dw];
  }
}

}  // namespace gfx
