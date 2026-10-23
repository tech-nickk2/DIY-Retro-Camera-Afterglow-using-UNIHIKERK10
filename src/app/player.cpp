#include "player.h"
#include <string.h>
#include "avi.h"
#include "esp_heap_caps.h"
#include "esp_jpg_decode.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../gfx/canvas.h"
#include "../hal/audio.h"

namespace player {

static constexpr size_t CHUNK_CAP = 64 * 1024;
static constexpr size_t RING = audio::RATE;  // 1 s of sound

static AviReader s_avi;
static uint8_t* s_chunk = nullptr;
static uint16_t* s_frame = nullptr;  // decoded, clip size
static int16_t* s_ring = nullptr;
static volatile size_t s_head = 0, s_tail = 0;
static volatile bool s_playing = false, s_audioRunning = false;
static int64_t s_t0 = 0;
static uint32_t s_shown = 0;  // video frames read so far
static bool s_ended = false;

struct Src { const uint8_t* p; size_t len; };

static size_t reader(void* arg, size_t index, uint8_t* buf, size_t len) {
  Src* s = (Src*)arg;
  if (index >= s->len) return 0;
  if (index + len > s->len) len = s->len - index;
  if (buf) memcpy(buf, s->p + index, len);
  return len;
}

// The decoder hands over RGB888 blocks; store them as panel-order RGB565.
static bool writer(void*, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t* data) {
  if (!data) return true;
  const int fw = s_avi.width(), fh = s_avi.height();
  for (int j = 0; j < h; j++) {
    if (y + j >= fh) break;
    uint16_t* d = s_frame + (y + j) * fw + x;
    const uint8_t* s = data + j * w * 3;
    for (int i = 0; i < w && x + i < fw; i++, s += 3) d[i] = gfx::rgb(s[0], s[1], s[2]);
  }
  return true;
}

static void audioTask(void*) {
  static int16_t buf[256];
  while (s_playing) {
    if (s_head - s_tail >= 256) {
      for (int i = 0; i < 256; i++) buf[i] = s_ring[(s_tail + i) % RING];
      s_tail = s_tail + 256;
      audio::play(buf, 256);
    } else {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
  }
  s_audioRunning = false;
  vTaskDelete(nullptr);
}

bool open(const char* path) {
  if (!s_chunk) {
    s_chunk = (uint8_t*)heap_caps_malloc(CHUNK_CAP, MALLOC_CAP_SPIRAM);
    s_frame = (uint16_t*)heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM);
    s_ring = (int16_t*)heap_caps_malloc(RING * 2, MALLOC_CAP_SPIRAM);
    if (!s_chunk || !s_frame || !s_ring) return false;
  }
  if (!s_avi.open(path)) return false;
  if (s_avi.width() > 320 || s_avi.height() > 240 || s_avi.width() <= 0) { s_avi.close(); return false; }
  memset(s_frame, 0, 320 * 240 * 2);
  s_head = 0;
  s_tail = 0;
  s_shown = 0;
  s_ended = false;
  s_playing = true;
  s_audioRunning = true;
  s_t0 = esp_timer_get_time();
  xTaskCreatePinnedToCore(audioTask, "playaudio", 3072, nullptr, 5, nullptr, 0);
  return true;
}

bool update(uint16_t* out, bool* changed) {
  *changed = false;
  if (s_ended) return false;
  const float t = (esp_timer_get_time() - s_t0) / 1e6f;
  const uint32_t due = (uint32_t)(t * s_avi.fps()) + 1;
  bool decoded = false;
  while (s_shown < due) {
    char kind;
    size_t len;
    if (!s_avi.next(&kind, s_chunk, CHUNK_CAP, &len)) { s_ended = true; break; }
    if (kind == 'a') {
      const int16_t* pcm = (const int16_t*)s_chunk;
      for (size_t i = 0; i < len / 2; i++) {
        if (s_head - s_tail >= RING) break;  // speaker fell behind: drop
        s_ring[s_head % RING] = pcm[i];
        s_head = s_head + 1;
      }
      continue;
    }
    s_shown++;
    if (s_shown < due) continue;  // behind: skip decoding this frame
    Src src = {s_chunk, len};
    decoded = esp_jpg_decode(len, JPG_SCALE_NONE, reader, writer, &src) == ESP_OK;
  }
  if (decoded) {
    gfx::scaleNearest(s_frame, s_avi.width(), s_avi.height(), out, 320, 240);
    *changed = true;
  }
  return !s_ended || s_head - s_tail > 0;
}

float progress() {
  const uint32_t total = s_avi.totalFrames();
  return total ? (float)s_shown / total : 0;
}

void close() {
  s_playing = false;
  while (s_audioRunning) vTaskDelay(pdMS_TO_TICKS(5));
  s_avi.close();
}

}  // namespace player
