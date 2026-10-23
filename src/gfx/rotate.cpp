#include "rotate.h"

namespace gfx {

static constexpr int MAX_DIM = 1600;
static constexpr int TILE = 16;

void rotateCrop(const uint16_t* src, int sw, int sh, uint16_t* dst, int dw, int dh, uint8_t orient) {
  static int rowOff[MAX_DIM];  // per output column: start of the source row it reads
  static int colIdx[MAX_DIM];  // per output row: the source column it reads
  if (dw > MAX_DIM || dh > MAX_DIM) return;
  const int cw = cropWidth(sw, sh), ch = cropHeight(sw, sh);
  const int v0 = (sw - ch) / 2;
  const bool turn = orient & ORIENT_TURN, mirror = orient & ORIENT_MIRROR;
  // Output column x reads source row r(u); output row y reads source column c(v).
  // One way: r = sh-1-u, c = v.  The other way: r = u, c = sw-1-v.
  for (int x = 0; x < dw; x++) {
    int u = (mirror ? dw - 1 - x : x) * cw / dw;
    rowOff[x] = (turn ? u : sh - 1 - u) * sw;
  }
  for (int y = 0; y < dh; y++) {
    int v = v0 + (int)((int64_t)y * ch / dh);
    colIdx[y] = turn ? sw - 1 - v : v;
  }
  // Work in strips of 16 output columns: each strip reads 16 source rows in
  // order and writes a narrow band of the output, so both stay in cache
  // even though the rotation turns rows into columns.
  for (int x0 = 0; x0 < dw; x0 += TILE) {
    const int x1 = x0 + TILE < dw ? x0 + TILE : dw;
    for (int y = 0; y < dh; y++) {
      uint16_t* d = dst + y * dw;
      const int v = colIdx[y];
      for (int x = x0; x < x1; x++) d[x] = src[rowOff[x] + v];
    }
  }
}

void swapBytes(uint16_t* px, int n) {
  for (int i = 0; i < n; i++) px[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));
}

}  // namespace gfx
