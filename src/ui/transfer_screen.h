// The Wi-Fi transfer screen: a QR code to join the camera's network (then
// one to open the gallery), the network name and password, and live status.
#pragma once
#include <stdint.h>
#include "../app/transfer.h"
#include "../gfx/canvas.h"

namespace ui {

struct Qr {
  int size = 0;              // modules per side, 0 if encoding failed
  uint8_t bits[45 * 45] = {};  // 1 = dark module
};

bool makeQr(const char* text, Qr* out);
// joinQr: true shows the join-the-network code, false the open-the-gallery code.
void drawTransfer(gfx::Canvas& c, const transfer::Status& st, const Qr& qr, bool joinQr, uint32_t nowMs);

}  // namespace ui
