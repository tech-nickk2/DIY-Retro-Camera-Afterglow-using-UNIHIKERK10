#include "gallery.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../hal/sdcard.h"

namespace gallery {

static const char* DIR_ = "/sd/AFTERGLOW";
static constexpr uint32_t MAGIC = 0x314C4741;  // "AGL1"

struct __attribute__((packed)) Header {
  uint32_t magic;
  uint16_t num;
  uint8_t stock, frame, kind, pad;  // kind was "instant" (0/1) before clips existed
  uint16_t tenths;
  uint32_t time;
};

static void path(char* out, int len, uint16_t num, const char* ext) {
  snprintf(out, len, "%s/%04u.%s", DIR_, num, ext);
}

bool save(const Item& m, const uint16_t* print) {
  if (!sdcard::mounted() || !sdcard::ensureDir(DIR_)) return false;
  char p[48];
  // Thumbnail: nearest-neighbour from the screen-size print, with the header in front.
  static uint16_t thumb[THUMB_W * THUMB_H];
  for (int y = 0; y < THUMB_H; y++)
    for (int x = 0; x < THUMB_W; x++) thumb[y * THUMB_W + x] = print[(y * 240 / THUMB_H) * 320 + x * 320 / THUMB_W];
  path(p, sizeof(p), m.num, "THM");
  FILE* f = fopen(p, "wb");
  if (!f) return false;
  Header h = {MAGIC, m.num, m.stock, m.frame, m.kind, 0, m.tenths, m.time};
  bool ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(thumb, 2, THUMB_W * THUMB_H, f) == (size_t)(THUMB_W * THUMB_H);
  fclose(f);
  path(p, sizeof(p), m.num, "PRV");
  f = fopen(p, "wb");
  if (!f) return false;
  ok = ok && fwrite(print, 2, 320 * 240, f) == 320 * 240;
  fclose(f);
  return ok;
}

int list(Item* out, int max) {
  DIR* d = opendir(DIR_);
  if (!d) return 0;
  int n = 0;
  while (struct dirent* e = readdir(d)) {
    const char* dot = strrchr(e->d_name, '.');
    if (!dot || strcasecmp(dot, ".THM") != 0 || n >= max) continue;
    char p[300];
    snprintf(p, sizeof(p), "%s/%s", DIR_, e->d_name);
    FILE* f = fopen(p, "rb");
    if (!f) continue;
    Header h;
    if (fread(&h, sizeof(h), 1, f) == 1 && h.magic == MAGIC)
      out[n++] = {h.num, h.stock, h.frame, h.kind, h.tenths, h.time};
    fclose(f);
  }
  closedir(d);
  qsort(out, n, sizeof(Item), [](const void* a, const void* b) {
    return (int)((const Item*)b)->num - (int)((const Item*)a)->num;
  });
  return n;
}

bool loadThumb(uint16_t num, uint16_t* out) {
  char p[48];
  path(p, sizeof(p), num, "THM");
  FILE* f = fopen(p, "rb");
  if (!f) return false;
  const bool ok = fseek(f, sizeof(Header), SEEK_SET) == 0 &&
                  fread(out, 2, THUMB_W * THUMB_H, f) == (size_t)(THUMB_W * THUMB_H);
  fclose(f);
  return ok;
}

bool loadPreview(uint16_t num, uint16_t* out) {
  char p[48];
  path(p, sizeof(p), num, "PRV");
  FILE* f = fopen(p, "rb");
  if (!f) return false;
  const bool ok = fread(out, 2, 320 * 240, f) == 320 * 240;
  fclose(f);
  return ok;
}

void mediaPath(const Item& it, char* out, int len) {
  snprintf(out, len, "/sd/DCIM/AFT_%04u.%s", it.num, it.kind == CLIP ? "AVI" : "JPG");
}

bool remove(const Item& it) {
  char p[48];
  mediaPath(it, p, sizeof(p));
  const bool ok = ::remove(p) == 0;
  path(p, sizeof(p), it.num, "THM");
  ::remove(p);
  path(p, sizeof(p), it.num, "PRV");
  ::remove(p);
  return ok;
}

}  // namespace gallery
