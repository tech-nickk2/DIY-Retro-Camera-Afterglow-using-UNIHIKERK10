#include "transfer_screen.h"
#include <stdio.h>
#include <string.h>
#include "qrcode.h"
#include "../gfx/font5x7.h"

using namespace gfx;

namespace ui {

static Qr* s_target = nullptr;

static void capture(esp_qrcode_handle_t q) {
  const int n = esp_qrcode_get_size(q);
  if (n > 45) { s_target->size = 0; return; }
  s_target->size = n;
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) s_target->bits[y * n + x] = esp_qrcode_get_module(q, x, y) ? 1 : 0;
}

bool makeQr(const char* text, Qr* out) {
  s_target = out;
  out->size = 0;
  esp_qrcode_config_t cfg = {};
  cfg.display_func = capture;
  cfg.max_qrcode_version = 7;  // up to 45 modules
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
  esp_qrcode_generate(&cfg, text);
  return out->size > 0;
}

void drawTransfer(Canvas& c, const transfer::Status& st, const Qr& qr, bool joinQr, uint32_t nowMs) {
  c.fill(rgb(0x14, 0x11, 0x10));
  for (int i = 0; i < 5; i++) c.fillRect(12 + i * 3, 8, 3, 15, col::STRIPES[i]);
  c.text(34, 9, "WI-FI TRANSFER", col::CREAM, 2, Align::Left, false);

  // The QR code on a white card with a quiet zone.
  const int box = 150, bx = 8, by = 34;
  c.fillRect(bx, by, box, box, col::WHITE);
  if (qr.size) {
    const int scale = box / (qr.size + 6), offs = (box - scale * qr.size) / 2;
    for (int y = 0; y < qr.size; y++)
      for (int x = 0; x < qr.size; x++)
        if (qr.bits[y * qr.size + x]) c.fillRect(bx + offs + x * scale, by + offs + y * scale, scale, scale, col::BLACK);
  }
  c.text(bx + box / 2, by + box + 6, joinQr ? "1 SCAN: JOIN WI-FI" : "2 SCAN: OPEN GALLERY", col::AMBER, 1, Align::Center, false);

  // Details on the right.
  const int x = 170;
  char l[40];
  c.text(x, 36, "NETWORK", col::DIM, 1, Align::Left, false);
  c.text(x, 47, st.ssid, col::CREAM, 1, Align::Left, false);
  c.text(x, 64, "PASSWORD", col::DIM, 1, Align::Left, false);
  c.text(x, 75, st.pass, col::AMBER, 2, Align::Left, false);
  c.text(x, 98, "THEN OPEN", col::DIM, 1, Align::Left, false);
  snprintf(l, sizeof(l), "%s", st.url);
  c.text(x, 109, l, col::CREAM, 1, Align::Left, false);

  snprintf(l, sizeof(l), "PHONES %d", st.stations);
  c.text(x, 134, l, st.stations ? col::GREEN : col::DIM, 1, Align::Left, false);
  c.text(x, 148, st.timeSynced ? "CLOCK SET" : "CLOCK: WAITING", st.timeSynced ? col::GREEN : col::DIM, 1, Align::Left, false);
  snprintf(l, sizeof(l), "SENT %lu  " G_DOT "  %.1f MB", (unsigned long)st.files, st.bytes / 1048576.f);
  c.text(x, 162, l, col::DIM, 1, Align::Left, false);
  if (st.stations && (nowMs / 500) % 2 == 0) c.fillCircle(x + 70, 137, 2, col::GREEN);

  c.text(W / 2, 226, "PRESS SWITCH QR  " G_DOT "  HOLD EXIT", col::DIM, 1, Align::Center, false);
}

}  // namespace ui
