// Effects that make cine mode look like Super 8 film or a 90s camcorder.
// They run on the recorded frame (240x180) after the colour pass, so what
// you see is exactly what gets recorded.
#pragma once
#include <stdint.h>

namespace cinefx {

// Super 8: gate weave, flicker, dust and hairs, and the rounded film gate.
void super8(uint16_t* px, int w, int h);

// Camcorder: colour bleed, scanlines and the odd tracking glitch.
// nowMs drives when a tracking band appears.
void camcorder(uint16_t* px, int w, int h, uint32_t nowMs);

// The camcorder date and time, burned into the bottom-left of the frame.
void burnCamcorderDate(uint16_t* px, int w, int h);

// Silent film: heavy flicker and weave, scratches running down the frame.
void silent(uint16_t* px, int w, int h);

// Security camera: light scanlines, noise, and "CAM 01" with a running
// timestamp burned in.
void cctv(uint16_t* px, int w, int h);

// Handheld console camera: chunky pixels, ordered dither, four greens.
void pocket(uint16_t* px, int w, int h);

// Early webcam: chunky pixels and fewer colours (the recorder adds heavy
// JPEG compression on top).
void webcam(uint16_t* px, int w, int h);

}  // namespace cinefx
