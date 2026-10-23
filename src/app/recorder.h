// Cine recording: the main loop hands over finished frames (with the look
// already applied), a task on core 0 encodes them to JPEG and writes them
// into an AVI, and a mic task collects 16 kHz sound in between.
#pragma once
#include <stdint.h>

namespace recorder {

constexpr int W = 240, H = 180;  // recorded frame size

struct Options {
  int quality = 82;     // JPEG quality; low for the webcam look's artefacts
  bool audio = true;    // silent film records no sound
  float speed = 1.f;    // playback speed; silent film plays back undercranked (faster)
};
bool start(const char* path, const Options& opt);
// Copies the frame if the encoder has a free slot; otherwise the frame is
// skipped (the clip's frame rate is measured, so sound stays in sync).
void pushFrame(const uint16_t* px);
struct Result { bool ok; uint32_t frames, durationMs; };
Result stop();
bool recording();
uint32_t elapsedMs();
uint32_t framesWritten();

}  // namespace recorder
