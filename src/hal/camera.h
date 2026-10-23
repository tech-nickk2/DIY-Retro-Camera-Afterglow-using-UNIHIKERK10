// GC2145 through esp32-camera, RGB565 only (the sensor has no JPEG engine).
// The driver cannot change resolution on the fly for raw formats, so a photo
// restarts the camera at capture size and then goes back to preview size.
#pragma once
#include <stdint.h>
#include "esp_camera.h"

namespace camera {

// Viewfinder sizes. Bigger is sharper but slower: at the 10 MHz camera clock
// the sensor can only send so many pixels per second. 640x480 was preferred
// on a real K10.
struct PreviewSize { framesize_t fs; const char* name; };
constexpr PreviewSize PREVIEW_SIZES[] = {
  {FRAMESIZE_QVGA, "320X240"}, {FRAMESIZE_CIF, "400X296"}, {FRAMESIZE_HVGA, "480X320"}, {FRAMESIZE_VGA, "640X480"},
};
constexpr int PREVIEW_COUNT = sizeof(PREVIEW_SIZES) / sizeof(PREVIEW_SIZES[0]);
constexpr int PREVIEW_DEFAULT = 3;

// Photo sizes, largest first; the first one the camera accepts is used.
constexpr framesize_t CAPTURE_SIZES[] = {FRAMESIZE_UXGA, FRAMESIZE_SXGA, FRAMESIZE_XGA, FRAMESIZE_SVGA};

// Pulses the reset line on the expander. Call once before the first start().
void hardReset();
// xclkHz 0 = keep the last clock used (starts at K10_CAM_XCLK_HZ).
// wide (with FRAMESIZE_QVGA): the photo-matching viewfinder, see WIDE_W.
bool start(framesize_t size, int fbCount, int xclkHz = 0, bool wide = false);

// The wide viewfinder. At 640x480 the driver reads only the middle 1280x960
// of the sensor, so the viewfinder showed 80% of what a photo captures. This
// mode reads 1024 columns x all 1200 rows (the photo uses the middle 900
// columns and every row) and shrinks it 4x, giving 256x300: the same pixel
// count as 320x240, which is all the driver checks.
constexpr int WIDE_W = 256, WIDE_H = 300;
int xclk();
void stop();
bool running();

camera_fb_t* grab();
void release(camera_fb_t* fb);

// Exposure carried across a camera restart. A restart for a full-size
// photo resets the sensor, and its auto exposure then needs several slow
// full-size frames to adapt: outdoors the photo came out white. Reading the
// viewfinder's settled exposure first and writing it back after the restart
// lets auto exposure carry on from the right place.
struct Exposure {
  uint16_t lines;  // exposure time in sensor rows
  uint8_t gain;    // global gain
  bool valid;
};
Exposure readExposure();
void seedExposure(const Exposure& e);
// Turns the sensor's auto exposure off and sets exposure and gain directly.
// Writing the registers with auto exposure on does not stick: it keeps its
// own state and overwrites them every frame.
void manualExposure(uint16_t lines, uint8_t gain);
// Sensor columns read per row in the current mode. Exposure is counted in
// rows, and a row takes longer when more columns are read, so this converts
// the viewfinder's exposure into the photo mode's.
int windowWidth();
int exposureBias();
// Auto exposure brightness target; ev -2..+2 (0 = the sensor's default).
void setExposureBias(int ev);

// flip: bit 0 = horizontal mirror, bit 1 = vertical flip
void setFlip(uint8_t flip);
uint8_t flip();

}  // namespace camera
