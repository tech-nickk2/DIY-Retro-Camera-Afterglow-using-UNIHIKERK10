#include "camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "xl9535.h"
#include "../board/k10_pins.h"

static const char* TAG = "camera";
static bool s_running = false;
static uint8_t s_flip = 0;
static int s_ev = 0;
static bool s_wide = false;
static int s_winW = 1600;

// The window width the driver picks for a frame size (its subsample table,
// smallest ratio first, skipping 1/3 for QVGA and larger).
static int driverWindowWidth(framesize_t fs) {
  static const struct { int w, h; } RES[] = {{96, 96}, {160, 120}, {128, 128}, {176, 144}, {240, 176}, {240, 240},
                                             {320, 240}, {320, 320}, {400, 296}, {480, 320}, {640, 480}, {800, 600},
                                             {1024, 768}, {1280, 720}, {1280, 1024}, {1600, 1200}};
  if (fs > FRAMESIZE_UXGA) return 1600;
  const int w = RES[fs].w, h = RES[fs].h;
  static const int NUM[] = {140, 210, 240, 252, 280, 420};
  for (int i = fs >= FRAMESIZE_QVGA ? 1 : 0; i < 6; i++)
    if (1600 * NUM[i] / 420 >= w && 1200 * NUM[i] / 420 >= h) return w * 420 / NUM[i];
  return 1600;
}

// GC2145 registers: 0xFE selects the page. Page 0: 0x03/0x04 exposure rows,
// 0xB0 global gain. Page 1: 0x13 auto exposure target brightness.
static constexpr int REG_PAGE = 0xFE;

static int rd(sensor_t* s, int page, int reg) {
  s->set_reg(s, REG_PAGE, 0xFF, page);
  return s->get_reg(s, reg, 0xFF);
}

static void wr(sensor_t* s, int page, int reg, int v) {
  s->set_reg(s, REG_PAGE, 0xFF, page);
  s->set_reg(s, reg, 0xFF, v);
}

// Custom window and subsampling, written after the driver's own setup.
static void applyWide() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  const int winW = 1024, winH = 1200, colS = (1600 - winW) / 2, rowS = 0;
  auto w = [&](int reg, int v) { s->set_reg(s, reg, 0xFF, v); };
  s->set_reg(s, REG_PAGE, 0xFF, 0);
  w(0x90, 0x01);  // crop enable
  w(0x09, rowS >> 8); w(0x0A, rowS & 0xFF);
  w(0x0B, colS >> 8); w(0x0C, colS & 0xFF);
  w(0x0D, (winH + 8) >> 8); w(0x0E, (winH + 8) & 0xFF);
  w(0x0F, (winW + 16) >> 8); w(0x10, (winW + 16) & 0xFF);
  w(0x99, 0x44);  // subsample 1/4 in both directions
  for (int r = 0x9B; r <= 0xA2; r++) w(r, 0x00);
  w(0x95, camera::WIDE_H >> 8); w(0x96, camera::WIDE_H & 0xFF);
  w(0x97, camera::WIDE_W >> 8); w(0x98, camera::WIDE_W & 0xFF);
}

static void applyBias() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  // The default target is 0x40; each step is half a stop.
  static const uint8_t TARGETS[5] = {0x20, 0x2D, 0x40, 0x5A, 0x78};
  wr(s, 1, 0x13, TARGETS[s_ev + 2]);
  s->set_reg(s, REG_PAGE, 0xFF, 0);  // the driver expects page 0
}
static int s_xclk = K10_CAM_XCLK_HZ;

static void applyFlip() {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_hmirror(s, (s_flip & 1) ? 1 : 0);
  s->set_vflip(s, (s_flip & 2) ? 1 : 0);
}

namespace camera {

void hardReset() {
  xl9535::write(K10_EXP_CAM_RST, false);
  vTaskDelay(pdMS_TO_TICKS(10));
  xl9535::write(K10_EXP_CAM_RST, true);
  vTaskDelay(pdMS_TO_TICKS(10));
}

bool start(framesize_t size, int fbCount, int xclkHz, bool wide) {
  if (s_running) stop();
  if (xclkHz > 0) s_xclk = xclkHz;
  camera_config_t c = {};
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.pin_xclk = K10_CAM_XCLK;
  c.pin_sccb_sda = -1;  // reuse the bus i2cbus already owns
  c.pin_sccb_scl = -1;
  c.sccb_i2c_port = K10_I2C_PORT;
  c.pin_d7 = K10_CAM_D7;
  c.pin_d6 = K10_CAM_D6;
  c.pin_d5 = K10_CAM_D5;
  c.pin_d4 = K10_CAM_D4;
  c.pin_d3 = K10_CAM_D3;
  c.pin_d2 = K10_CAM_D2;
  c.pin_d1 = K10_CAM_D1;
  c.pin_d0 = K10_CAM_D0;
  c.pin_vsync = K10_CAM_VSYNC;
  c.pin_href = K10_CAM_HREF;
  c.pin_pclk = K10_CAM_PCLK;
  c.xclk_freq_hz = s_xclk;
  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size = size;
  c.jpeg_quality = 12;
  c.fb_count = fbCount;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = fbCount > 1 ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    return false;
  }
  s_running = true;
  s_wide = wide && size == FRAMESIZE_QVGA;
  s_winW = s_wide ? 1024 : driverWindowWidth(size);
  if (s_wide) applyWide();
  applyFlip();
  applyBias();
  return true;
}

void stop() {
  if (!s_running) return;
  esp_camera_deinit();
  s_running = false;
}

bool running() { return s_running; }
int xclk() { return s_xclk; }

camera_fb_t* grab() {
  camera_fb_t* fb = s_running ? esp_camera_fb_get() : nullptr;
  if (fb && s_wide) {  // the driver reports the nominal 320x240
    fb->width = WIDE_W;
    fb->height = WIDE_H;
  }
  return fb;
}
void release(camera_fb_t* fb) { if (fb) esp_camera_fb_return(fb); }

Exposure readExposure() {
  Exposure e = {0, 0, false};
  sensor_t* s = esp_camera_sensor_get();
  if (!s || !s_running) return e;
  const int hi = rd(s, 0, 0x03), lo = rd(s, 0, 0x04), g = rd(s, 0, 0xB0);
  if (hi < 0 || lo < 0 || g < 0) return e;
  e.lines = (uint16_t)(((hi & 0x1F) << 8) | lo);
  e.gain = (uint8_t)g;
  e.valid = e.lines > 0;
  ESP_LOGI(TAG, "exposure: %u rows, gain 0x%02X", e.lines, e.gain);
  return e;
}

void seedExposure(const Exposure& e) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s || !e.valid) return;
  wr(s, 0, 0x03, (e.lines >> 8) & 0x1F);
  wr(s, 0, 0x04, e.lines & 0xFF);
  wr(s, 0, 0xB0, e.gain);
}

void manualExposure(uint16_t lines, uint8_t gain) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  if (lines < 1) lines = 1;
  if (lines > 0x1FFF) lines = 0x1FFF;
  s->set_reg(s, REG_PAGE, 0xFF, 0);
  s->set_reg(s, 0xB6, 0x01, 0x00);  // [0] AEC enable: off
  s->set_reg(s, 0x03, 0xFF, (lines >> 8) & 0x1F);
  s->set_reg(s, 0x04, 0xFF, lines & 0xFF);
  s->set_reg(s, 0xB0, 0xFF, gain);
}

int windowWidth() { return s_winW; }
int exposureBias() { return s_ev; }

void setExposureBias(int ev) {
  s_ev = ev < -2 ? -2 : ev > 2 ? 2 : ev;
  if (s_running) applyBias();
}

void setFlip(uint8_t flip) {
  s_flip = flip & 3;
  applyFlip();
}

uint8_t flip() { return s_flip; }

}  // namespace camera
