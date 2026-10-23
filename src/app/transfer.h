// Wi-Fi transfer: the camera opens its own network (no router needed) and
// serves a small gallery page, so a phone can view and save photos and
// clips. Opening the page also sends the phone's time to the camera, which
// fixes the clock (the K10 has no clock battery and no internet here).
#pragma once
#include <stdint.h>
#include <time.h>

namespace transfer {

struct Status {
  char ssid[24];
  char pass[12];
  char url[24];
  int stations;      // phones connected to the network
  bool timeSynced;   // a phone has set the clock this session
  uint32_t files;    // files sent
  uint32_t bytes;    // bytes sent
};

// password: the network password to use (8+ characters).
bool start(const char* password);
void stop();
bool running();
void loop();  // serves requests; call often
const Status& status();

}  // namespace transfer
