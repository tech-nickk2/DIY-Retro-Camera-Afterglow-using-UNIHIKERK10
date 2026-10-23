// RGB565 drawing surface. Pixels are stored byte-swapped (panel and camera
// order), so camera frames, film lookup tables and the LCD all work on the
// same values with no conversion. Always build colours with rgb().
// begin() allocates the 320x240 screen frame; wrap() draws on any buffer,
// such as a full-size photo.
#pragma once
#include <stdint.h>

namespace gfx {

constexpr int W = 320;
constexpr int H = 240;

constexpr uint16_t swap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return swap16((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)));
}

namespace col {
constexpr uint16_t BLACK = rgb(0, 0, 0);
constexpr uint16_t WHITE = rgb(255, 255, 255);
constexpr uint16_t AMBER = rgb(0xF5, 0xB4, 0x1C);
constexpr uint16_t CREAM = rgb(0xF3, 0xEA, 0xD8);
constexpr uint16_t DIM = rgb(0x8F, 0x87, 0x7B);
constexpr uint16_t DARKDIM = rgb(0x6F, 0x6A, 0x62);
constexpr uint16_t RED = rgb(0xFF, 0x5A, 0x4A);
constexpr uint16_t GREEN = rgb(0x8D, 0xFF, 0x9E);
constexpr uint16_t DATE = rgb(0xFF, 0x9A, 0x2E);
constexpr uint16_t STRIPES[5] = {rgb(0xD8, 0x35, 0x2A), rgb(0xEE, 0x7A, 0x1C), rgb(0xF5, 0xB4, 0x1C),
                                 rgb(0x5A, 0xA3, 0x6A), rgb(0x2F, 0x77, 0xB8)};
}  // namespace col

enum class Align : uint8_t { Left, Center, Right };

class Canvas {
 public:
  bool begin();                                  // allocates a W x H frame in PSRAM
  void wrap(uint16_t* px, int w, int h);         // draws on an existing buffer
  uint16_t* pixels() { return px_; }
  int width() const { return w_; }
  int height() const { return h_; }

  // Drawing is limited to this rectangle until resetClip().
  void setClip(int x, int y, int w, int h);
  void resetClip();

  void fill(uint16_t c);
  void pixel(int x, int y, uint16_t c) {
    if (x >= cx0_ && x < cx1_ && y >= cy0_ && y < cy1_) px_[y * w_ + x] = c;
  }
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void rect(int x, int y, int w, int h, uint16_t c);
  void hline(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void vline(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void line(int x0, int y0, int x1, int y1, uint16_t c);
  void circle(int cx, int cy, int r, uint16_t c);
  void fillCircle(int cx, int cy, int r, uint16_t c);
  // Arc from angle a0 to a1 (radians, 0 = right, clockwise because y points down).
  void arc(int cx, int cy, int r, float a0, float a1, uint16_t c, int thickness = 1);
  // Darkens a region: keep is 0..256 (256 = unchanged).
  void dim(int x, int y, int w, int h, int keep);

  // 5x7 pixel font, 6*scale px per character. Returns the text width.
  int text(int x, int y, const char* s, uint16_t c, int scale = 1, Align a = Align::Left, bool shadow = true);
  static int textWidth(const char* s, int scale = 1);
  // Seven-segment digits (0-9, space, '-', 'S', 'E', apostrophe, colon).
  int seg7(int x, int y, const char* s, int w, int h, int t, int gap, uint16_t c);
  static int seg7Width(const char* s, int w, int t, int gap);

 private:
  void glyphs(int x, int y, const char* s, uint16_t c, int scale);
  uint16_t* px_ = nullptr;
  int w_ = W, h_ = H;
  int cx0_ = 0, cy0_ = 0, cx1_ = W, cy1_ = H;
};

// Nearest-neighbour scale of a whole buffer (both in the same pixel order).
void scaleNearest(const uint16_t* src, int sw, int sh, uint16_t* dst, int dw, int dh);

}  // namespace gfx
