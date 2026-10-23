// ILI9341 over SPI2. The panel stays in its native portrait mode (240x320,
// MADCTL MY|BGR, the orientation proven on the K10 by xiaozhi-esp32), and the
// 320x240 landscape canvas is rotated in software while it is copied into
// the DMA bounce buffers. The canvas is already in panel byte order.
#pragma once
#include <stdint.h>

namespace display {

constexpr int W = 320;  // landscape canvas size
constexpr int H = 240;

// 0: USB-C on the left, 1: USB-C on the right
constexpr int ROTATION_COUNT = 2;

bool begin(int rotation);
void setRotation(int rotation);
int rotation();
void push(const uint16_t* fb);
void backlight(bool on);

}  // namespace display
