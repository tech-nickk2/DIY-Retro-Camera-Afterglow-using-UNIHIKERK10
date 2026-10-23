#include "recorder.h"
#include <string.h>
#include "avi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "../hal/audio.h"

static const char* TAG = "recorder";

namespace recorder {

static constexpr int SLOTS = 3;
static constexpr size_t JPEG_CAP = 64 * 1024;
static constexpr size_t RING = audio::RATE * 2;  // 2 s of mono samples

static AviWriter s_avi;
static uint16_t* s_slot[SLOTS];
static QueueHandle_t s_full = nullptr, s_free = nullptr;
static uint8_t* s_jpeg = nullptr;
static size_t s_jpegLen = 0;
static int16_t* s_ring = nullptr;
static volatile size_t s_ringHead = 0, s_ringTail = 0;
static volatile bool s_running = false, s_micRunning = false, s_encRunning = false;
static int64_t s_t0 = 0;
static bool s_ok = true;
static Options s_opt;
static uint32_t s_encodeMs = 0, s_encoded = 0, s_dropped = 0;

static size_t jpgToMem(void*, size_t, const void* data, size_t len) {
  if (s_jpegLen + len > JPEG_CAP) return 0;
  memcpy(s_jpeg + s_jpegLen, data, len);
  s_jpegLen += len;
  return len;
}

// Reads both mics, mixes them, high-passes at ~90 Hz and fills the ring.
static void micTask(void*) {
  static int16_t l[256], r[256];
  float y = 0, prev = 0;
  for (int i = 0; i < 7; i++) audio::record(l, r, 256);  // drop what sat in the DMA buffers (~0.1 s)
  while (s_running) {
    const size_t n = audio::record(l, r, 256);
    for (size_t i = 0; i < n; i++) {
      const float x = (l[i] + r[i]) * .5f;
      y = .965f * (y + x - prev);
      prev = x;
      const int v = (int)y;
      s_ring[s_ringHead % RING] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
      s_ringHead = s_ringHead + 1;
    }
  }
  s_micRunning = false;
  vTaskDelete(nullptr);
}

static void drainAudio() {
  static int16_t buf[2048];
  size_t head = s_ringHead;
  if (head - s_ringTail > RING) s_ringTail = head - RING;  // fell behind: drop the oldest
  while (s_ringTail < head) {
    size_t n = head - s_ringTail;
    if (n > 2048) n = 2048;
    for (size_t i = 0; i < n; i++) buf[i] = s_ring[(s_ringTail + i) % RING];
    if (!s_avi.addAudio(buf, n)) s_ok = false;
    s_ringTail += n;
  }
}

static void encTask(void*) {
  int idx;
  for (;;) {
    if (xQueueReceive(s_full, &idx, pdMS_TO_TICKS(100)) != pdTRUE) {
      if (!s_running) break;
      continue;
    }
    if (idx < 0) break;  // stop marker
    s_jpegLen = 0;
    const int64_t t0 = esp_timer_get_time();
    if (fmt2jpg_cb((uint8_t*)s_slot[idx], W * H * 2, W, H, PIXFORMAT_RGB565, s_opt.quality, jpgToMem, nullptr)) {
      if (!s_avi.addFrame(s_jpeg, s_jpegLen)) s_ok = false;
    }
    xQueueSend(s_free, &idx, 0);
    if (s_opt.audio) drainAudio();
    s_encodeMs += (uint32_t)((esp_timer_get_time() - t0) / 1000);
    s_encoded++;
  }
  if (s_opt.audio) drainAudio();
  s_encRunning = false;
  vTaskDelete(nullptr);
}

bool start(const char* path, const Options& opt) {
  if (s_running) return false;
  if (!s_full) {
    s_full = xQueueCreate(SLOTS + 1, sizeof(int));
    s_free = xQueueCreate(SLOTS, sizeof(int));
    for (auto& p : s_slot) p = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
    s_jpeg = (uint8_t*)heap_caps_malloc(JPEG_CAP, MALLOC_CAP_SPIRAM);
    s_ring = (int16_t*)heap_caps_malloc(RING * 2, MALLOC_CAP_SPIRAM);
    if (!s_jpeg || !s_ring || !s_slot[SLOTS - 1]) return false;
  }
  xQueueReset(s_full);
  xQueueReset(s_free);
  for (int i = 0; i < SLOTS; i++) xQueueSend(s_free, &i, 0);
  if (!s_avi.open(path, W, H, audio::RATE)) { ESP_LOGE(TAG, "can't open %s", path); return false; }
  s_ok = true;
  s_opt = opt;
  s_encodeMs = s_encoded = s_dropped = 0;
  s_ringHead = 0;
  s_ringTail = 0;
  s_running = true;
  s_micRunning = opt.audio;
  s_encRunning = true;
  s_t0 = esp_timer_get_time();
  if (opt.audio) xTaskCreatePinnedToCore(micTask, "mic", 4096, nullptr, 6, nullptr, 0);
  // Not pinned: the encoder also gets core 1's idle time while the screen is
  // being sent over SPI, which was the main limit on the frame rate.
  xTaskCreate(encTask, "enc", 8192, nullptr, 2, nullptr);
  return true;
}

void pushFrame(const uint16_t* px) {
  if (!s_running) return;
  int idx;
  if (xQueueReceive(s_free, &idx, 0) != pdTRUE) { s_dropped++; return; }  // encoder busy: skip this frame
  memcpy(s_slot[idx], px, W * H * 2);
  xQueueSend(s_full, &idx, 0);
}

Result stop() {
  if (!s_running) return {false, 0, 0};
  const uint32_t dur = (uint32_t)((esp_timer_get_time() - s_t0) / 1000);
  s_running = false;
  const int stopMark = -1;
  xQueueSend(s_full, &stopMark, portMAX_DELAY);
  while (s_encRunning || s_micRunning) vTaskDelay(pdMS_TO_TICKS(10));
  const uint32_t frames = s_avi.frames();
  // The header's frame rate sets the playback speed; silent film plays faster.
  const bool closed = s_avi.close((uint32_t)(dur / (s_opt.speed > 0 ? s_opt.speed : 1)));
  ESP_LOGI(TAG, "clip: %u frames in %u ms (%.1f fps), encode %.0f ms/frame, %u skipped (encoder busy)",
           (unsigned)frames, (unsigned)dur, frames * 1000.f / (dur ? dur : 1),
           s_encoded ? (float)s_encodeMs / s_encoded : 0.f, (unsigned)s_dropped);
  return {s_ok && closed && frames > 0, frames, dur};
}

bool recording() { return s_running; }
uint32_t elapsedMs() { return s_running ? (uint32_t)((esp_timer_get_time() - s_t0) / 1000) : 0; }
uint32_t framesWritten() { return s_avi.frames(); }

}  // namespace recorder
