#include "film.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"

using gfx::rgb;
using gfx::swap16;

static const char* TAG = "film";

namespace film {

// Same numbers as the mockup's STOCKS table.
const Stock STOCKS[] = {
  {"gold", "GOLD 200", 200, "WARM " "\x80" " SUNNY " "\x80" " FORGIVING", rgb(0xF2, 0xB7, 0x05), rgb(0xC9, 0x30, 0x2C), 10, .40f, 0,
   {1.15f, 1.15f, {.04f, .03f, .02f}, {1, .97f, .88f}, {0, .01f, .03f}, {.05f, .03f, -.03f}, {0, 0, 0}}},
  {"soft", "SOFT 160", 160, "PASTEL SKIN " "\x80" " LOW CONTRAST", rgb(0xEC, 0xE4, 0xD6), rgb(0xE0, 0x7B, 0x39), 7, .30f, 0,
   {.85f, .9f, {.07f, .07f, .07f}, {.98f, .97f, .93f}, {0, .025f, .02f}, {.04f, .02f, 0}, {0, 0, 0}}},
  {"mono", "MONO 400", 400, "GRITTY B&W " "\x80" " DEEP BLACKS", rgb(0x1D, 0x1D, 0x1D), rgb(0xF1, 0xF1, 0xF1), 20, .45f, 0,
   {1, 1.4f, {.03f, .03f, .03f}, {.97f, .97f, .97f}, {0, 0, 0}, {0, 0, 0}, {.42f, .46f, .12f}}},
  {"xpro", "XPRO 100", 100, "WRONG CHEMISTRY " "\x80" " WILD", rgb(0x3F, 0xA3, 0x4D), rgb(0xE9, 0xF0, 0x6A), 9, .50f, 0,
   {1.35f, 1.45f, {0, .02f, .08f}, {1, 1, .75f}, {-.02f, .03f, .08f}, {.04f, .06f, -.08f}, {0, 0, 0}}},
  {"night", "NIGHT 800", 800, "TUNGSTEN " "\x80" " BLUE " "\x80" " GRAINY", rgb(0x1F, 0x3F, 0x8F), rgb(0x9E, 0xC9, 0xFF), 26, .40f, 0,
   {1.05f, 1.1f, {.03f, .04f, .07f}, {.8f, .92f, 1.02f}, {0, .01f, .05f}, {.02f, 0, .03f}, {0, 0, 0}}},
  {"instant", "INSTANT", 600, "SOFT " "\x80" " FADED " "\x80" " SELF DEVELOPING", rgb(0x2B, 0x2A, 0x2C), rgb(0xE9, 0xE6, 0xDF), 6, .35f, 10,
   {.8f, .85f, {.1f, .08f, .09f}, {.95f, .93f, .88f}, {0, .03f, .04f}, {.05f, .02f, 0}, {0, 0, 0}}},
};
const int STOCK_COUNT = sizeof(STOCKS) / sizeof(STOCKS[0]);
const Stock NO_FILM = {"none", "NO FILM", 200, "", rgb(0x55, 0x55, 0x55), rgb(0x99, 0x99, 0x99), 4, .05f, 0,
                       {1, 1, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};

const Stock SUPER8 = {"s8", "SUPER 8", 200, "", rgb(0xC9, 0x30, 0x2C), rgb(0xF2, 0xB7, 0x05), 22, .70f, 0,
                      {1.1f, 1.25f, {.06f, .04f, .02f}, {1, .93f, .8f}, {0, .01f, .03f}, {.05f, .03f, -.04f}, {0, 0, 0}}};
const Stock CAMCORDER = {"vhs", "CAMCORDER", 200, "", rgb(0x22, 0x22, 0x22), rgb(0x9E, 0xC9, 0xFF), 6, .12f, 0,
                         {.88f, 1.05f, {.05f, .05f, .08f}, {1, .97f, .95f}, {0, 0, .03f}, {.02f, .01f, 0}, {0, 0, 0}}};

const Stock SILENT = {"silent", "SILENT FILM", 200, "", rgb(0x3A, 0x2A, 0x1A), rgb(0xE8, 0xD8, 0xB0), 28, .85f, 0,
                      {1, 1.35f, {.08f, .06f, .04f}, {1, .9f, .72f}, {0, 0, 0}, {0, 0, 0}, {.40f, .45f, .15f}}};
const Stock CCTV = {"cctv", "CCTV", 200, "", rgb(0x22, 0x33, 0x22), rgb(0xAA, 0xCC, 0xAA), 16, .25f, 0,
                    {1, .9f, {.04f, .06f, .04f}, {.82f, .95f, .82f}, {0, 0, 0}, {0, 0, 0}, {.30f, .60f, .10f}}};
const Stock POCKET = {"pocket", "POCKET CAM", 200, "", rgb(0x30, 0x62, 0x30), rgb(0x9B, 0xBC, 0x0F), 0, 0, 0,
                      {1, 1.15f, {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
const Stock WEBCAM = {"webcam", "WEBCAM", 200, "", rgb(0x10, 0x30, 0x80), rgb(0xD0, 0xE0, 0xFF), 5, .20f, 0,
                      {1.35f, 1.15f, {.02f, .02f, .04f}, {.95f, 1, 1.08f}, {0, 0, .03f}, {.03f, .03f, 0}, {0, 0, 0}}};

static const Stock* s_cur = &NO_FILM;
static uint16_t* s_lut = nullptr;
static bool s_lutInternal = false;
static int8_t s_grain[128 * 128];
static bool s_grainReady = false;

int indexOf(const char* id) {
  for (int i = 0; i < STOCK_COUNT; i++)
    if (strcmp(STOCKS[i].id, id) == 0) return i;
  return -1;
}

static float toneCurve(float x, float k) {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  return x < .5f ? .5f * powf(2 * x, k) : 1 - .5f * powf(2 * (1 - x), k);
}

static void buildTable(const Params& p) {
  // The tone curve is the only costly step, so it is tabulated first.
  static float tone[1025];
  for (int i = 0; i <= 1024; i++) tone[i] = toneCurve(i / 1024.f, p.k);
  auto T = [](float x) { return x <= 0 ? tone[0] : x >= 1 ? tone[1024] : tone[(int)(x * 1024 + .5f)]; };
  const bool mono = p.mono[0] + p.mono[1] + p.mono[2] > 0;
  for (uint32_t i = 0; i < 65536; i++) {
    const uint16_t c = swap16((uint16_t)i);  // table is indexed by panel-order pixels
    const int r5 = c >> 11, g6 = (c >> 5) & 63, b5 = c & 31;
    float ch[3] = {((r5 << 3) | (r5 >> 2)) / 255.f, ((g6 << 2) | (g6 >> 4)) / 255.f, ((b5 << 3) | (b5 >> 2)) / 255.f};
    if (mono) {
      ch[0] = ch[1] = ch[2] = ch[0] * p.mono[0] + ch[1] * p.mono[1] + ch[2] * p.mono[2];
    } else {
      float l = .299f * ch[0] + .587f * ch[1] + .114f * ch[2];
      for (float& v : ch) v = l + (v - l) * p.sat;
    }
    for (float& v : ch) v = T(v);
    const float l2 = .299f * ch[0] + .587f * ch[1] + .114f * ch[2];
    const float sw = (1 - l2) * (1 - l2), hw = l2 * l2;
    int out[3];
    for (int k = 0; k < 3; k++) {
      float v = ch[k] + p.sh[k] * sw + p.hi[k] * hw;
      v = p.lift[k] + v * (p.gain[k] - p.lift[k]);
      int q = (int)lroundf(v * 255);
      out[k] = q < 0 ? 0 : q > 255 ? 255 : q;
    }
    s_lut[i] = rgb((uint8_t)out[0], (uint8_t)out[1], (uint8_t)out[2]);
  }
}

bool select(const Stock& s) {
  if (!s_lut) {
    // 128 KB. Internal RAM is much faster for random lookups; PSRAM is the fallback.
    s_lut = (uint16_t*)heap_caps_malloc(65536 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_lutInternal = s_lut != nullptr;
    if (!s_lut) s_lut = (uint16_t*)heap_caps_malloc(65536 * 2, MALLOC_CAP_SPIRAM);
    if (!s_lut) return false;
    ESP_LOGI(TAG, "film table in %s RAM", s_lutInternal ? "internal" : "PSRAM");
  }
  if (!s_grainReady) {
    // Roughly Gaussian grain: sum of three uniform randoms, about +-63.
    for (int i = 0; i < 128 * 128; i++) {
      int a = (int)(esp_random() & 0xFF) + (int)(esp_random() & 0xFF) + (int)(esp_random() & 0xFF) - 382;
      s_grain[i] = (int8_t)(a / 6);
    }
    s_grainReady = true;
  }
  if (s_cur == &s && s_cur != &NO_FILM) return true;
  buildTable(s.p);
  s_cur = &s;
  return true;
}

const Stock& current() { return *s_cur; }

void release() {
  if (s_lut) heap_caps_free(s_lut);
  s_lut = nullptr;
  s_lutInternal = false;
  s_cur = &NO_FILM;  // forces a rebuild on the next select()
}
bool tableInternal() { return s_lutInternal; }

void apply(uint16_t* px, int w, int h, float grainScale, uint32_t seed) {
  if (!s_lut) return;
  const Stock& st = *s_cur;
  // Separable vignette: (1 - s*dx^2)(1 - s*dy^2) is close to the mockup's radial
  // falloff and costs one multiply per pixel.
  static uint16_t vcol[1600];
  if (w > 1600) return;
  const float s = st.vig * .64f, half = w * .5f;
  for (int x = 0; x < w; x++) {
    float dx = (x - half + .5f) / half;
    vcol[x] = (uint16_t)(256 * (1 - s * dx * dx));
  }
  const int amp = (int)(st.grain * grainScale * 6.1f);  // grain texture is about +-63 -> +-grain*1.5 levels
  const int ox = seed & 127, oy = (seed >> 7) & 127;
  for (int y = 0; y < h; y++) {
    float dy = (y - h * .5f + .5f) / half;
    const int vrow = (int)(256 * (1 - s * dy * dy));
    const int8_t* grow = s_grain + ((y + oy) & 127) * 128;
    uint16_t* row = px + (size_t)y * w;
    for (int x = 0; x < w; x++) {
      const uint16_t c = swap16(s_lut[row[x]]);
      const int vg = (vcol[x] * vrow) >> 8;
      const int n8 = (grow[(x + ox) & 127] * amp) >> 8;  // in 8-bit levels
      int r = (((c >> 11) * vg) >> 8) + (n8 >> 3);
      int g = ((((c >> 5) & 63) * vg) >> 8) + (n8 >> 2);
      int b = (((c & 31) * vg) >> 8) + (n8 >> 3);
      r = r < 0 ? 0 : r > 31 ? 31 : r;
      g = g < 0 ? 0 : g > 63 ? 63 : g;
      b = b < 0 ? 0 : b > 31 ? 31 : b;
      row[x] = swap16((uint16_t)((r << 11) | (g << 5) | b));
    }
  }
}

void dateText(char* out, int len) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_year < 120) { out[0] = 0; return; }  // clock not set yet
  snprintf(out, len, "'%02d %d %d", t.tm_year % 100, t.tm_mon + 1, t.tm_mday);
}

void dateStamp(gfx::Canvas& c) {
  char s[16];
  dateText(s, sizeof(s));
  if (!s[0]) return;
  // Sized relative to the frame so it looks the same on the screen and in the photo.
  const float k = c.width() / 320.f;
  const int w = (int)(7 * k), h = (int)(12 * k), t = (int)(2 * k) > 0 ? (int)(2 * k) : 1, gap = (int)(3 * k);
  const int tw = gfx::Canvas::seg7Width(s, w, t, gap);
  const int x = c.width() - (int)(14 * k) - tw, y = c.height() - (int)(14 * k) - h;
  // A soft orange glow first, then the brighter core: the look of an LED imprint.
  const int g = t / 2 + 1;
  for (int dy = -g; dy <= g; dy += g)
    for (int dx = -g; dx <= g; dx += g) c.seg7(x + dx, y + dy, s, w, h, t, gap, rgb(0x9A, 0x40, 0x10));
  c.seg7(x, y, s, w, h, t, gap, rgb(0xFF, 0xB0, 0x4A));
}

}  // namespace film
