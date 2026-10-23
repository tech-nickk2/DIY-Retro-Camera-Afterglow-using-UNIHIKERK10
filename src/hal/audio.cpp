#include "audio.h"
#include <math.h>
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "../board/k10_pins.h"

static const char* TAG = "audio";
static i2s_chan_handle_t s_tx = nullptr, s_rx = nullptr;
static i2c_master_dev_handle_t s_adc = nullptr;
static uint8_t s_adcAddr = 0;
static int32_t s_vol = 256;  // master volume, 256 = unity


// esp-adf es7243e.c init sequence, in order, with the K10 profile overrides
// (0x0B, 0x0E) reported to work on real hardware.
static const uint8_t ES7243E_INIT[][2] = {
  {0x01, 0x3A}, {0x00, 0x80}, {0xF9, 0x00}, {0x04, 0x02}, {0x04, 0x01}, {0xF9, 0x01}, {0x00, 0x1E}, {0x01, 0x00},
  {0x02, 0x00}, {0x03, 0x20}, {0x04, 0x01}, {0x0D, 0x00}, {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF},
  {0x09, 0xCA}, {0x0A, 0x85}, {0x0B, 0x2C}, {0x0E, 0xFF}, {0x0F, 0x80}, {0x14, 0x0C}, {0x15, 0x0C}, {0x17, 0x02},
  {0x18, 0x26}, {0x19, 0x77}, {0x1A, 0xF4}, {0x1B, 0x66}, {0x1C, 0x44}, {0x1E, 0x00}, {0x1F, 0x0C},
  {0x00, 0x80}, {0x01, 0x3A}, {0x16, 0x3F}, {0x16, 0x00},
};

static bool initAdc() {
  for (uint8_t a = 0x10; a <= 0x17; a++) {
    if (i2cbus::probe(a)) { s_adcAddr = a; break; }
  }
  if (!s_adcAddr) {
    ESP_LOGW(TAG, "ES7243E not found (scanned 0x10-0x17)");
    return false;
  }
  s_adc = i2cbus::add(s_adcAddr, 100000);
  for (auto& r : ES7243E_INIT) {
    if (!i2cbus::writeReg(s_adc, r[0], r[1])) {
      ESP_LOGE(TAG, "ES7243E write 0x%02X failed", r[0]);
      return false;
    }
  }
  ESP_LOGI(TAG, "ES7243E at 0x%02X", s_adcAddr);
  audio::setMicGain(24, 12);  // cleanest split by ear on a real K10
  return true;
}

namespace audio {

bool begin() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;  // silence, not old data, when we stop writing
  if (i2s_new_channel(&cc, &s_tx, &s_rx) != ESP_OK) {
    ESP_LOGE(TAG, "I2S channel failed");
    return false;
  }
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)K10_I2S_MCLK,
      .bclk = (gpio_num_t)K10_I2S_BCLK,
      .ws = (gpio_num_t)K10_I2S_WS,
      .dout = (gpio_num_t)K10_I2S_DOUT,
      .din = (gpio_num_t)K10_I2S_DIN,
      .invert_flags = {},
    },
  };
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK || i2s_channel_init_std_mode(s_rx, &sc) != ESP_OK) {
    ESP_LOGE(TAG, "I2S std mode failed");
    return false;
  }
  i2s_channel_enable(s_tx);
  i2s_channel_enable(s_rx);
  vTaskDelay(pdMS_TO_TICKS(10));  // the ADC needs MCLK running before it is configured
  initAdc();
  return true;
}

bool speakerOk() { return s_tx != nullptr; }
void setVolume(float v) { s_vol = (int32_t)(v * 256); }
float volume() { return s_vol / 256.f; }
bool micOk() { return s_adc != nullptr; }
uint8_t micAddr() { return s_adcAddr; }

void play(const int16_t* mono, size_t n) {
  if (!s_tx) return;
  int16_t buf[512];
  while (n) {
    size_t k = n > 256 ? 256 : n;
    for (size_t i = 0; i < k; i++) {
      int32_t v = (mono[i] * s_vol) >> 8;
      v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
      buf[2 * i] = buf[2 * i + 1] = (int16_t)v;
    }
    size_t written;
    i2s_channel_write(s_tx, buf, k * 4, &written, portMAX_DELAY);
    mono += k;
    n -= k;
  }
}

void tone(float hz, int ms, float vol, bool square) {
  if (!s_tx) return;
  int16_t buf[256];
  size_t total = (size_t)RATE * ms / 1000, done = 0;
  float phase = 0, step = hz / RATE;
  int16_t amp = (int16_t)(vol * 32767);
  size_t fade = RATE / 200;  // 5 ms ramps so the tone does not click
  while (done < total) {
    size_t k = total - done > 256 ? 256 : total - done;
    for (size_t i = 0; i < k; i++, done++) {
      float s = square ? (phase < 0.5f ? 1.f : -1.f) : sinf(phase * 6.2831853f);
      float env = 1.f;
      if (done < fade) env = (float)done / fade;
      else if (total - done < fade) env = (float)(total - done) / fade;
      buf[i] = (int16_t)(s * amp * env);
      phase += step;
      if (phase >= 1.f) phase -= 1.f;
    }
    play(buf, k);
  }
}

size_t record(int16_t* left, int16_t* right, size_t n) {
  if (!s_rx) return 0;
  int16_t buf[512];
  size_t got = 0;
  while (got < n) {
    size_t want = n - got > 256 ? 256 : n - got, rd = 0;
    if (i2s_channel_read(s_rx, buf, want * 4, &rd, pdMS_TO_TICKS(200)) != ESP_OK) break;
    size_t frames = rd / 4;
    for (size_t i = 0; i < frames; i++) {
      left[got + i] = buf[2 * i];
      if (right) right[got + i] = buf[2 * i + 1];
    }
    got += frames;
  }
  return got;
}

void setMicGain(int analogDb, int digitalDb) {
  if (!s_adc) return;
  if (analogDb < 0) analogDb = 0;
  if (analogDb > 30) analogDb = 30;
  if (digitalDb < 0) digitalDb = 0;
  if (digitalDb > 32) digitalDb = 32;
  uint8_t pga = 0x10 | (analogDb / 3);          // 0x10 = PGA on
  uint8_t vol = (uint8_t)(0xBF + digitalDb * 2);  // 0xBF = 0 dB, 0.5 dB per step
  i2cbus::writeReg(s_adc, 0x20, pga);
  i2cbus::writeReg(s_adc, 0x21, pga);
  i2cbus::writeReg(s_adc, 0x0E, vol);
  ESP_LOGI(TAG, "mic gain: analog +%d dB, digital +%d dB", analogDb, digitalDb);
}

}  // namespace audio
