// The GC2145 sits on the board turned 90 degrees from how the camera is held,
// so every frame is rotated a quarter turn and a centred 4:3 landscape crop is
// taken from it (the sensor's short side becomes the photo's long side).
#pragma once
#include <stdint.h>

namespace gfx {

// Size of the landscape crop for a sw x sh sensor frame.
inline int cropWidth(int sw, int sh) { return sh; }
inline int cropHeight(int sw, int sh) { return sh * 3 / 4; }

// orient: bit 0 turns the other way (a 180 degree difference), bit 1 mirrors.
// Done here because the GC2145's own mirror/flip settings have no effect.
enum : uint8_t { ORIENT_TURN = 1, ORIENT_MIRROR = 2 };

// Rotates src (sw x sh) a quarter turn, crops it to 4:3 landscape and scales
// it (nearest neighbour) into dst (dw x dh, both <= 1600).
void rotateCrop(const uint16_t* src, int sw, int sh, uint16_t* dst, int dw, int dh, uint8_t orient);

// Swaps the two bytes of every pixel in place.
void swapBytes(uint16_t* px, int n);

}  // namespace gfx
