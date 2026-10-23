// Cine mode on-screen displays from the mockup: the Super 8 viewfinder
// (footage in feet of a 50 ft cartridge) and the camcorder OSD.
#pragma once
#include <stdint.h>
#include "../gfx/canvas.h"

namespace ui {

enum class CineLook : uint8_t { Super8 = 0, Camcorder = 1, Silent = 2, Cctv = 3, Pocket = 4, Webcam = 5 };
constexpr int CINE_LOOK_COUNT = 6;
const char* cineLookName(uint8_t look);

void drawCineOsd(gfx::Canvas& c, CineLook look, bool recording, uint32_t recMs, float feet, uint32_t nowMs);

// Playback overlay: progress bar and hints.
void drawPlayback(gfx::Canvas& c, float progress);

}  // namespace ui
