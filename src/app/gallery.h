// The album's own copy of each photo and clip. Next to every AFT_NNNN.JPG
// or .AVI it keeps a thumbnail and a screen-size still as raw RGB565 in
// /sd/AFTERGLOW, so the album opens instantly instead of decoding JPEGs.
#pragma once
#include <stdint.h>

namespace gallery {

constexpr int THUMB_W = 100, THUMB_H = 75;

enum Kind : uint8_t { PHOTO = 0, INSTANT = 1, CLIP = 2 };

struct Item {
  uint16_t num;       // matches AFT_<num>.JPG or .AVI
  uint8_t stock;      // index into film::STOCKS (photos), or the cine look (clips)
  uint8_t frame;      // frame number on its roll
  uint8_t kind;       // Kind
  uint16_t tenths;    // clip length in tenths of a second
  uint32_t time;      // when it was taken
};

// print: the developed photo (or a clip's first frame) at 320x240.
bool save(const Item& meta, const uint16_t* print);
// Newest first. Returns the number of items written to out.
int list(Item* out, int max);
bool loadThumb(uint16_t num, uint16_t* out);    // THUMB_W x THUMB_H
bool loadPreview(uint16_t num, uint16_t* out);  // 320 x 240
// Deletes the photo or clip and the album's copies.
bool remove(const Item& it);
// Full path of the photo or clip file.
void mediaPath(const Item& it, char* out, int len);

}  // namespace gallery
