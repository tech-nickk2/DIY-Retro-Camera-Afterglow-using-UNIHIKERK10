// Plays an AFT_NNNN.AVI clip: JPEG frames decoded to the screen at the clip's
// real frame rate (frames are dropped if decoding falls behind), sound
// streamed to the speaker from its own task.
#pragma once
#include <stdint.h>

namespace player {

bool open(const char* path);
// Draws the frame that is due now into out (320x240) if it changed.
// Returns false when the clip has ended.
bool update(uint16_t* out, bool* changed);
float progress();  // 0..1
void close();

}  // namespace player
