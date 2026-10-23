#include "avi.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

// Offsets of the fields patched at close, relative to the start of the file.
// Layout: RIFF hdr (12) | LIST hdrl (12) | avih (8+56) | LIST strl (12) | strh vids (8+56)
//         | strf (8+40) | LIST strl (12) | strh auds (8+56) | strf (8+16) | LIST movi (12)
enum : long {
  OFF_RIFF_SIZE = 4,
  OFF_AVIH = 12 + 12 + 8,             // avih data
  OFF_STRH_V = OFF_AVIH + 56 + 12 + 8,  // video strh data
  OFF_STRF_V = OFF_STRH_V + 56 + 8,
  OFF_STRH_A = OFF_STRF_V + 40 + 12 + 8,
  OFF_STRF_A = OFF_STRH_A + 56 + 8,
  OFF_MOVI_LIST = OFF_STRF_A + 16,      // 'LIST' of the movi list
  HEADER_END = OFF_MOVI_LIST + 12,
};

static void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }

static void buildHeader(uint8_t* h, int w, int hgt, int rate, uint32_t frames, uint32_t samples, uint32_t durMs,
                        uint32_t maxChunk, uint32_t riffSize, uint32_t moviSize) {
  memset(h, 0, HEADER_END);
  memcpy(h, "RIFF", 4); put32(h + 4, riffSize); memcpy(h + 8, "AVI ", 4);
  memcpy(h + 12, "LIST", 4); put32(h + 16, OFF_MOVI_LIST - 20); memcpy(h + 20, "hdrl", 4);
  // Frame rate as the exact ratio frames / duration.
  const uint32_t scale = durMs ? durMs : 1000, fr = durMs ? frames * 1000 : 18000;
  uint8_t* a = h + OFF_AVIH - 8;
  memcpy(a, "avih", 4); put32(a + 4, 56);
  a += 8;
  put32(a + 0, frames && durMs ? (uint32_t)((uint64_t)durMs * 1000 / frames) : 55555);  // us per frame
  put32(a + 4, 400000);
  put32(a + 12, 0x10);  // AVIF_HASINDEX
  put32(a + 16, frames);
  put32(a + 24, 2);  // streams
  put32(a + 28, maxChunk);
  put32(a + 32, w);
  put32(a + 36, hgt);
  uint8_t* l = h + OFF_AVIH + 56;
  memcpy(l, "LIST", 4); put32(l + 4, 4 + 64 + 48); memcpy(l + 8, "strl", 4);
  uint8_t* s = h + OFF_STRH_V - 8;
  memcpy(s, "strh", 4); put32(s + 4, 56);
  s += 8;
  memcpy(s, "vids", 4); memcpy(s + 4, "MJPG", 4);
  put32(s + 20, scale); put32(s + 24, fr); put32(s + 32, frames); put32(s + 36, maxChunk);
  put32(s + 40, 0xFFFFFFFF);
  put16(s + 52, w); put16(s + 54, hgt);
  uint8_t* f = h + OFF_STRF_V - 8;
  memcpy(f, "strf", 4); put32(f + 4, 40);
  f += 8;
  put32(f, 40); put32(f + 4, w); put32(f + 8, hgt); put16(f + 12, 1); put16(f + 14, 24);
  memcpy(f + 16, "MJPG", 4); put32(f + 20, w * hgt * 3);
  l = h + OFF_STRF_V + 40;
  memcpy(l, "LIST", 4); put32(l + 4, 4 + 64 + 24); memcpy(l + 8, "strl", 4);
  s = h + OFF_STRH_A - 8;
  memcpy(s, "strh", 4); put32(s + 4, 56);
  s += 8;
  memcpy(s, "auds", 4);
  put32(s + 20, 1); put32(s + 24, rate); put32(s + 32, samples); put32(s + 36, 8192);
  put32(s + 40, 0xFFFFFFFF); put32(s + 44, 2);
  f = h + OFF_STRF_A - 8;
  memcpy(f, "strf", 4); put32(f + 4, 16);
  f += 8;
  put16(f, 1); put16(f + 2, 1); put32(f + 4, rate); put32(f + 8, rate * 2); put16(f + 12, 2); put16(f + 14, 16);
  uint8_t* m = h + OFF_MOVI_LIST;
  memcpy(m, "LIST", 4); put32(m + 4, moviSize); memcpy(m + 8, "movi", 4);
}

bool AviWriter::open(const char* path, int w, int h, int audioRate) {
  f_ = fopen(path, "wb");
  if (!f_) return false;
  setvbuf(f_, nullptr, _IOFBF, 16 * 1024);
  w_ = w; h_ = h; rate_ = audioRate;
  frames_ = samples_ = maxChunk_ = 0;
  indexCap_ = 16 * 8192;
  index_ = (uint8_t*)heap_caps_malloc(indexCap_, MALLOC_CAP_SPIRAM);
  indexLen_ = 0;
  uint8_t hdr[HEADER_END];
  buildHeader(hdr, w, h, audioRate, 0, 0, 0, 0, 0, 4);
  fwrite(hdr, 1, HEADER_END, f_);
  moviStart_ = OFF_MOVI_LIST + 8;
  return index_ != nullptr;
}

bool AviWriter::chunk(const char* id, const void* data, size_t len, bool key) {
  if (!f_) return false;
  const long pos = ftell(f_);
  uint8_t h[8];
  memcpy(h, id, 4);
  put32(h + 4, (uint32_t)len);
  bool ok = fwrite(h, 1, 8, f_) == 8 && fwrite(data, 1, len, f_) == len;
  if (len & 1) fputc(0, f_);  // chunks are word aligned
  if (indexLen_ + 16 > indexCap_) {
    uint8_t* bigger = (uint8_t*)heap_caps_realloc(index_, indexCap_ * 2, MALLOC_CAP_SPIRAM);
    if (bigger) { index_ = bigger; indexCap_ *= 2; }
  }
  if (indexLen_ + 16 <= indexCap_) {
    uint8_t* e = index_ + indexLen_;
    memcpy(e, id, 4);
    put32(e + 4, key ? 0x10 : 0);
    put32(e + 8, (uint32_t)(pos - moviStart_));
    put32(e + 12, (uint32_t)len);
    indexLen_ += 16;
  }
  if (len > maxChunk_) maxChunk_ = len;
  return ok;
}

bool AviWriter::addFrame(const uint8_t* jpeg, size_t len) {
  if (!chunk("00dc", jpeg, len, true)) return false;
  frames_++;
  return true;
}

bool AviWriter::addAudio(const int16_t* pcm, size_t samples) {
  if (!samples) return true;
  if (!chunk("01wb", pcm, samples * 2, false)) return false;
  samples_ += samples;
  return true;
}

bool AviWriter::close(uint32_t durationMs) {
  if (!f_) return false;
  const long moviEnd = ftell(f_);
  uint8_t h[8];
  memcpy(h, "idx1", 4);
  put32(h + 4, (uint32_t)indexLen_);
  fwrite(h, 1, 8, f_);
  if (indexLen_) fwrite(index_, 1, indexLen_, f_);
  const long end = ftell(f_);
  uint8_t hdr[HEADER_END];
  buildHeader(hdr, w_, h_, rate_, frames_, samples_, durationMs, maxChunk_, (uint32_t)(end - 8),
              (uint32_t)(moviEnd - OFF_MOVI_LIST - 8));
  fseek(f_, 0, SEEK_SET);
  const bool ok = fwrite(hdr, 1, HEADER_END, f_) == (size_t)HEADER_END;
  fclose(f_);
  f_ = nullptr;
  heap_caps_free(index_);
  index_ = nullptr;
  return ok;
}

// ------------------------------------------------------------------ reader

bool AviReader::open(const char* path) {
  f_ = fopen(path, "rb");
  if (!f_) return false;
  uint8_t hdr[HEADER_END];
  if (fread(hdr, 1, HEADER_END, f_) != (size_t)HEADER_END || memcmp(hdr, "RIFF", 4) || memcmp(hdr + OFF_MOVI_LIST + 8, "movi", 4)) {
    close();
    return false;
  }
  uint32_t scale, rate, size;
  memcpy(&scale, hdr + OFF_STRH_V + 20, 4);
  memcpy(&rate, hdr + OFF_STRH_V + 24, 4);
  memcpy(&total_, hdr + OFF_AVIH + 16, 4);
  int32_t w, h;
  memcpy(&w, hdr + OFF_AVIH + 32, 4);
  memcpy(&h, hdr + OFF_AVIH + 36, 4);
  w_ = w; h_ = h;
  fps_ = scale ? (float)rate / scale : 18;
  if (fps_ < 1 || fps_ > 60) fps_ = 18;
  memcpy(&size, hdr + OFF_MOVI_LIST + 4, 4);
  moviEnd_ = OFF_MOVI_LIST + 8 + (long)size;
  setvbuf(f_, nullptr, _IOFBF, 8 * 1024);
  return true;
}

void AviReader::close() {
  if (f_) fclose(f_);
  f_ = nullptr;
}

bool AviReader::next(char* kind, uint8_t* buf, size_t cap, size_t* len) {
  while (f_ && ftell(f_) + 8 <= moviEnd_) {
    uint8_t h[8];
    if (fread(h, 1, 8, f_) != 8) return false;
    uint32_t n;
    memcpy(&n, h + 4, 4);
    const long padded = (long)n + (n & 1);
    const bool video = !memcmp(h + 2, "dc", 2), audio = !memcmp(h + 2, "wb", 2);
    if ((video || audio) && n <= cap) {
      if (fread(buf, 1, n, f_) != n) return false;
      if (n & 1) fgetc(f_);
      *kind = video ? 'v' : 'a';
      *len = n;
      return true;
    }
    fseek(f_, padded, SEEK_CUR);  // something else, or too big: skip it
  }
  return false;
}
