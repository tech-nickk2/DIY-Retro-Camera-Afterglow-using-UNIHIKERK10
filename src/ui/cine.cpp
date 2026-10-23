#include "cine.h"
#include <stdio.h>
#include "../gfx/font5x7.h"

using namespace gfx;

namespace ui {

static void battery(Canvas& c, int x, int y) {
  c.rect(x, y, 13, 7, col::WHITE);
  c.fillRect(x + 13, y + 2, 2, 3, col::WHITE);
  for (int i = 0; i < 3; i++) c.fillRect(x + 2 + i * 3, y + 2, 2, 3, col::WHITE);
}

const char* cineLookName(uint8_t look) {
  static const char* NAMES[CINE_LOOK_COUNT] = {"SUPER 8", "CAMCORDER", "SILENT FILM", "CCTV", "POCKET CAM", "WEBCAM"};
  return look < CINE_LOOK_COUNT ? NAMES[look] : "CINE";
}

static void recDot(Canvas& c, int x, int y, bool recording, bool blink, uint16_t idle, const char* idleText) {
  if (recording) {
    if (blink) c.text(x, y, G_REC, col::RED, 1);
    c.text(x + 9, y, "REC", col::RED, 1);
  } else {
    c.text(x, y, idleText, idle, 1);
  }
}

void drawCineOsd(Canvas& c, CineLook look, bool recording, uint32_t recMs, float feet, uint32_t nowMs) {
  const bool blink = (nowMs / 500) % 2 == 0;
  char l[24];
  const uint32_t secs = recMs / 1000;
  switch (look) {
    case CineLook::Silent: {
      const uint16_t k = rgb(0xE8, 0xD8, 0xB0);
      c.text(18, 12, "SILENT FILM", k, 1);
      c.text(W - 18, 12, "16 FPS " G_DOT " NO SOUND", k, 1, Align::Right);
      recDot(c, 18, 222, recording, blink, k, "READY");
      snprintf(l, sizeof(l), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
      c.text(W - 18, 222, l, k, 1, Align::Right);
      return;
    }
    case CineLook::Cctv:
      // CAM 01 and the timestamp are burned into the footage itself.
      recDot(c, W - 40, 8, recording, blink, col::WHITE, "LIVE");
      return;
    case CineLook::Pocket: {
      const uint16_t k = rgb(0x0F, 0x38, 0x0F);
      c.fillRect(0, 226, W, 14, rgb(0x9B, 0xBC, 0x0F));
      c.text(8, 229, "POCKET CAM", k, 1, Align::Left, false);
      if (recording) snprintf(l, sizeof(l), "%s REC %lu:%02lu", blink ? G_REC : " ", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
      else snprintf(l, sizeof(l), "READY");
      c.text(W - 8, 229, l, k, 1, Align::Right, false);
      return;
    }
    case CineLook::Webcam:
      // A little software window title bar.
      c.fillRect(0, 0, W, 13, rgb(0x10, 0x30, 0x80));
      c.text(6, 3, "WEBCAM  " G_DOT "  320X240", col::WHITE, 1, Align::Left, false);
      if (recording || blink) c.text(W - 6, 3, recording ? G_REC " REC" : G_REC " LIVE", recording ? col::RED : col::GREEN, 1, Align::Right, false);
      return;
    default: break;
  }
  if (look == CineLook::Super8) {
    const uint16_t k = rgb(0xF3, 0xE2, 0xC0);
    c.text(22, 16, "SUPER 8", k, 1);
    c.text(W - 22, 16, "18 FPS", k, 1, Align::Right);
    if (recording) {
      if (blink) c.text(22, 206, G_REC, col::RED, 1);
      c.text(31, 206, "RUN", col::RED, 1);
    } else {
      c.text(22, 206, "READY", k, 1);
    }
    snprintf(l, sizeof(l), "FT %02d/50", (int)feet);
    c.text(W - 22, 206, l, k, 1, Align::Right);
    c.fillRect(W - 78, 217, 56, 3, rgb(0x5A, 0x54, 0x48));
    const int fill = (int)(56 * (feet > 50 ? 1 : feet / 50));
    c.fillRect(W - 78, 217, fill > 0 ? fill : 1, 3, k);
    return;
  }
  if (recording) {
    if (blink) c.fillCircle(20, 21, 5, rgb(0xFF, 0x2E, 0x2E));
    c.text(30, 14, "REC", col::WHITE, 2);
  } else {
    c.text(14, 14, "STBY", col::WHITE, 2);
  }
  c.text(W - 36, 14, "SP", col::WHITE, 2, Align::Right);
  battery(c, W - 30, 18);
  const uint32_t s = recMs / 1000;
  snprintf(l, sizeof(l), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  c.text(W - 14, 216, l, col::WHITE, 2, Align::Right);
}

void drawPlayback(Canvas& c, float progress) {
  c.text(16, 12, G_RIGHT " PLAY", col::WHITE, 1);
  c.text(W - 16, 12, "B STOP", col::WHITE, 1, Align::Right);
  c.fillRect(16, 228, 288, 4, rgb(0, 0, 0));
  c.fillRect(16, 228, (int)(288 * (progress > 1 ? 1 : progress)), 4, col::AMBER);
}

}  // namespace ui
