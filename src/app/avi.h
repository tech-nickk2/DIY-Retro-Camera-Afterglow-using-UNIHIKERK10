// Minimal AVI (RIFF) container: MJPEG video plus 16-bit mono PCM audio.
// Written as the camera records, then the header and index are patched with
// the real frame count and frame rate when the clip is closed. Plays on PCs
// and phones, and in the camera's own album.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

class AviWriter {
 public:
  bool open(const char* path, int w, int h, int audioRate);
  bool addFrame(const uint8_t* jpeg, size_t len);
  bool addAudio(const int16_t* pcm, size_t samples);
  // durationMs sets the real frame rate so video and sound stay together.
  bool close(uint32_t durationMs);
  uint32_t frames() const { return frames_; }
  bool isOpen() const { return f_ != nullptr; }

 private:
  bool chunk(const char* id, const void* data, size_t len, bool key);
  FILE* f_ = nullptr;
  int w_ = 0, h_ = 0, rate_ = 0;
  uint32_t frames_ = 0, samples_ = 0, maxChunk_ = 0;
  long moviStart_ = 0;         // file offset of the 'movi' fourcc
  uint8_t* index_ = nullptr;   // idx1 entries, 16 bytes each
  size_t indexLen_ = 0, indexCap_ = 0;
};

class AviReader {
 public:
  bool open(const char* path);
  void close();
  float fps() const { return fps_; }
  uint32_t totalFrames() const { return total_; }
  int width() const { return w_; }
  int height() const { return h_; }
  // Next chunk in the movie. kind: 'v' video, 'a' audio. The data goes into
  // buf (up to cap bytes; bigger chunks are skipped). Returns false at the end.
  bool next(char* kind, uint8_t* buf, size_t cap, size_t* len);

 private:
  FILE* f_ = nullptr;
  float fps_ = 18;
  uint32_t total_ = 0;
  int w_ = 0, h_ = 0;
  long moviEnd_ = 0;
};
