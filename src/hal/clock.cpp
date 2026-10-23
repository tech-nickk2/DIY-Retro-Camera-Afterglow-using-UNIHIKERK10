#include "clock.h"
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_timer.h"

static Preferences s_prefs;
static int64_t s_lastSaveUs = 0;

static time_t buildTime() {
  // __DATE__ is "Sep 24 2026", __TIME__ is "14:03:07"
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
  struct tm t = {};
  t.tm_mon = (int)((strstr(months, mon) - months) / 3);
  t.tm_mday = atoi(__DATE__ + 4);
  t.tm_year = atoi(__DATE__ + 7) - 1900;
  t.tm_hour = atoi(__TIME__);
  t.tm_min = atoi(__TIME__ + 3);
  t.tm_sec = atoi(__TIME__ + 6);
  return mktime(&t);
}

static void save() {
  s_prefs.putULong64("clock", (uint64_t)time(nullptr));
  s_lastSaveUs = esp_timer_get_time();
}

namespace clock_ {

void begin() {
  s_prefs.begin("clock", false);
  time_t saved = (time_t)s_prefs.getULong64("clock", 0), built = buildTime();
  set(saved > built ? saved : built);
}

void tick() {
  if (esp_timer_get_time() - s_lastSaveUs > 60LL * 1000000) save();
}

void set(time_t t) {
  struct timeval tv = {t, 0};
  settimeofday(&tv, nullptr);
  save();
}

}  // namespace clock_
