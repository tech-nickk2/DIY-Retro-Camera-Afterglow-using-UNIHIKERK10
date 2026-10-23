#include "bringup.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "img_converters.h"
#include "../gfx/canvas.h"
#include "../gfx/font5x7.h"
#include "../gfx/rotate.h"
#include "../hal/audio.h"
#include "../hal/buttons.h"
#include "../hal/camera.h"
#include "../hal/display.h"
#include "../hal/encoder.h"
#include "../hal/i2c_bus.h"
#include "../hal/leds.h"
#include "../hal/sdcard.h"
#include "../hal/sensors.h"
#include "../hal/xl9535.h"

using namespace gfx;

static Canvas cv;
static Preferences prefs;

using camera::CAPTURE_SIZES;
#define PREVIEWS camera::PREVIEW_SIZES
#define PREVIEW_COUNT camera::PREVIEW_COUNT
static uint8_t s_preview = camera::PREVIEW_DEFAULT;
#define PREVIEW_SIZE (PREVIEWS[s_preview].fs)

struct Check {
  const char* name;
  char detail[24];
  int state;  // 0 pending, 1 ok, 2 failed, 3 warning
};
static Check checks[] = {
  {"I2C BUS", "", 0}, {"EXPANDER", "", 0}, {"DISPLAY", "", 0}, {"CAMERA", "", 0}, {"SD CARD", "", 0},
  {"SPEAKER", "", 0}, {"MIC ADC", "", 0}, {"RGB LEDS", "", 0}, {"ACCEL", "", 0}, {"LIGHT", "", 0}, {"BUTTONS", "", 0},
};
enum { C_I2C, C_EXP, C_DISP, C_CAM, C_SD, C_SPK, C_MIC, C_LED, C_ACC, C_ALS, C_BTN, C_COUNT };

static uint8_t s_rot = 0;
static uint8_t s_orient = ORIENT_TURN;  // see gfx/rotate.h; the K10 needs a turn
static uint32_t s_camLogT = 0;
static bool s_displayOk = false;

static uint32_t s_fpsT0 = 0;
static int s_fpsFrames = 0;
static float s_fps = 0;
static int16_t s_ax = 0, s_ay = 0, s_az = 0;
static float s_lux = -1;
static uint32_t s_sensT = 0;
static char s_toast[48] = "";
static uint32_t s_toastUntil = 0;
static char s_info[3][48] = {"", "", ""};
static uint32_t s_infoUntil = 0;

static void toast(const char* msg, uint32_t ms = 1600) {
  snprintf(s_toast, sizeof(s_toast), "%s", msg);
  s_toastUntil = millis() + ms;
}

static void present() {
  if (s_displayOk) display::push(cv.pixels());
}

static void set(int i, int state, const char* fmt = "", ...) {
  checks[i].state = state;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(checks[i].detail, sizeof(checks[i].detail), fmt, ap);
  va_end(ap);
  Serial.printf("[check] %-9s %s %s\n", checks[i].name, state == 1 ? "OK  " : state == 3 ? "WARN" : "FAIL", checks[i].detail);
}

static void drawChecklist() {
  cv.fill(rgb(10, 9, 8));
  for (int i = 0; i < 5; i++) cv.fillRect(0, 10 + i * 3, 60, 3, col::STRIPES[i]);
  cv.text(70, 10, "AFTERGLOW", col::CREAM, 2, Align::Left, false);
  cv.text(W - 10, 15, "BRING-UP", col::DIM, 1, Align::Right, false);
  for (int i = 0; i < C_COUNT; i++) {
    int y = 38 + i * 17;
    const Check& c = checks[i];
    uint16_t sc = c.state == 1 ? col::GREEN : c.state == 2 ? col::RED : c.state == 3 ? col::AMBER : col::DIM;
    const char* st = c.state == 1 ? "OK" : c.state == 2 ? "FAIL" : c.state == 3 ? "WARN" : "...";
    cv.text(16, y, c.name, col::CREAM, 1, Align::Left, false);
    cv.text(100, y, st, sc, 1, Align::Left, false);
    cv.text(140, y, c.detail, col::DIM, 1, Align::Left, false);
  }
}

static void showChecklist() {
  drawChecklist();
  present();
}

static void testPattern() {
  // Colour bars with labels: if a bar's colour does not match its label, the
  // panel's RGB/BGR bit (MADCTL in display.cpp) is wrong.
  const uint16_t bars[] = {rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), col::WHITE};
  const char* names[] = {"RED", "GREEN", "BLUE", "WHITE"};
  cv.fill(rgb(20, 20, 24));
  for (int i = 0; i < 4; i++) {
    cv.fillRect(i * 80, 60, 80, 120, bars[i]);
    cv.text(i * 80 + 40, 186, names[i], col::CREAM, 1, Align::Center, false);
  }
  cv.text(W / 2, 100, "NO CAMERA", col::BLACK, 2, Align::Center, false);
}

static void orientationMarks() {
  // An arrow in the top-left corner: it should point at the top-left of the
  // screen as you hold the camera, and read left to right.
  cv.fillRect(4, 4, 18, 3, col::AMBER);
  cv.fillRect(4, 4, 3, 18, col::AMBER);
  cv.line(4, 4, 22, 22, col::AMBER);
  cv.text(26, 18, "TOP LEFT", col::AMBER, 1);
}

static void copyFrame(const camera_fb_t* fb) {
  rotateCrop((const uint16_t*)fb->buf, fb->width, fb->height, cv.pixels(), W, H, s_orient);
}

// ---------------------------------------------------------------- actions

struct JpgSink { FILE* f; size_t bytes; };
static size_t jpgWrite(void* arg, size_t index, const void* data, size_t len) {
  JpgSink* s = (JpgSink*)arg;
  s->bytes += len;
  return s->f ? fwrite(data, 1, len, s->f) : len;
}

static void banner(const char* l1, const char* l2 = "") {
  cv.dim(0, 0, W, H, 90);
  cv.fillRect(40, 92, 240, 56, rgb(12, 10, 8));
  cv.hline(40, 92, 240, col::AMBER);
  cv.hline(40, 147, 240, col::AMBER);
  cv.text(W / 2, 102, l1, col::CREAM, 2, Align::Center, false);
  cv.text(W / 2, 128, l2, col::AMBER, 1, Align::Center, false);
  present();
}

static void takePhoto() {
  banner("CAPTURING", "HOLD STILL");
  audio::tone(2400, 40, 0.2f);
  leds::setAll(40, 40, 40);
  leds::show();

  uint32_t t0 = millis();
  camera::stop();
  framesize_t used = FRAMESIZE_INVALID;
  for (framesize_t fs : CAPTURE_SIZES) {
    if (camera::start(fs, 1)) { used = fs; break; }
  }
  uint32_t tInit = millis() - t0;
  camera_fb_t* fb = nullptr;
  uint32_t tGrab = 0, tLag = 0;  // tLag: button press to frame in hand
  if (used != FRAMESIZE_INVALID) {
    // The first frame after a restart can be stale; one is enough to drop.
    camera::release(camera::grab());
    uint32_t tg = millis();
    fb = camera::grab();
    tGrab = millis() - tg;
    tLag = millis() - t0;
  }
  leds::setAll(0, 0, 0);
  leds::show();

  if (!fb) {
    snprintf(s_info[0], 48, "CAPTURE FAILED");
    snprintf(s_info[1], 48, "INIT %lu MS", (unsigned long)tInit);
    s_info[2][0] = 0;
  } else {
    copyFrame(fb);
    banner("DEVELOPING", "ENCODING JPEG");
    // Rotate and crop the full frame to a landscape photo before encoding.
    const int pw = cropWidth(fb->width, fb->height), ph = cropHeight(fb->width, fb->height);
    uint32_t tr = millis();
    uint16_t* photo = (uint16_t*)heap_caps_malloc((size_t)pw * ph * 2, MALLOC_CAP_SPIRAM);
    if (photo) {
      rotateCrop((const uint16_t*)fb->buf, fb->width, fb->height, photo, pw, ph, s_orient);
    }
    uint32_t tRot = millis() - tr;
    camera::release(fb);  // frees the big sensor frame before encoding
    fb = nullptr;
    char name[40] = "";
    JpgSink sink = {nullptr, 0};
    if (sdcard::nextName("IMG_", "JPG", name, sizeof(name))) {
      sink.f = fopen(name, "wb");
      if (sink.f) setvbuf(sink.f, nullptr, _IOFBF, 16 * 1024);
    }
    uint32_t tj = millis();
    bool ok = photo && fmt2jpg_cb((uint8_t*)photo, (size_t)pw * ph * 2, pw, ph, PIXFORMAT_RGB565, 90, jpgWrite, &sink);
    bool saved = sink.f != nullptr;
    if (sink.f) fclose(sink.f);
    uint32_t tJpg = millis() - tj;
    if (photo) heap_caps_free(photo);
    snprintf(s_info[0], 48, "%s", !photo ? "OUT OF MEMORY" : !ok ? "JPEG FAILED" : saved ? name + 4 : "NO CARD - NOT SAVED");
    snprintf(s_info[1], 48, "%dX%d  %u KB  LAG %lu MS", pw, ph, (unsigned)(sink.bytes / 1024), (unsigned long)tLag);
    snprintf(s_info[2], 48, "INIT %lu GRAB %lu ROT %lu JPEG+SD %lu", (unsigned long)tInit, (unsigned long)tGrab,
             (unsigned long)tRot, (unsigned long)tJpg);
  }
  Serial.printf("[photo] %s | %s | %s\n", s_info[0], s_info[1], s_info[2]);
  s_infoUntil = millis() + 6000;

  camera::stop();
  camera::start(PREVIEW_SIZE, 2);
}

static void chime() {
  static const float notes[] = {659.3f, 784.0f, 987.8f, 1318.5f, 1568.0f};
  const uint8_t rgbs[5][3] = {{216, 53, 42}, {238, 122, 28}, {245, 180, 28}, {90, 163, 106}, {47, 119, 184}};
  for (int i = 0; i < 5; i++) {
    leds::setAll(0, 0, 0);
    leds::set(i % 3, rgbs[i][0] / 4, rgbs[i][1] / 4, rgbs[i][2] / 4);
    leds::show();
    audio::tone(notes[i], 110, 0.18f, false);
  }
  leds::setAll(0, 0, 0);
  leds::show();
}

static void writeWav(const char* path, const int16_t* s, size_t n) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  uint32_t dataLen = n * 2, riffLen = dataLen + 36, rate = audio::RATE, byteRate = rate * 2;
  uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0};
  memcpy(h + 4, &riffLen, 4);
  memcpy(h + 24, &rate, 4);
  memcpy(h + 28, &byteRate, 4);
  h[32] = 2; h[34] = 16;
  memcpy(h + 36, "data", 4);
  memcpy(h + 40, &dataLen, 4);
  fwrite(h, 1, 44, f);
  fwrite(s, 2, n, f);
  fclose(f);
}

// Mic gain splits to compare. Each keeps roughly the same total loudness but
// moves gain from the ADC's digital stage (noisier) to the analog PGA.
struct MicGain { int analog, digital; };
static const MicGain MIC_GAINS[] = {{6, 32}, {18, 18}, {24, 12}, {30, 6}};
static constexpr int MIC_GAIN_COUNT = sizeof(MIC_GAINS) / sizeof(MIC_GAINS[0]);
static uint8_t s_micGain = 2;  // +24/+12: cleanest by ear on a real K10

struct ChanStats { int dc, peak; float floorDb; };

// DC offset, peak, and the noise floor: the quietest 50 ms stretch, in dBFS.
static ChanStats analyse(int16_t* s, size_t n) {
  ChanStats st = {0, 0, 0};
  if (!n) return st;
  int64_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += s[i];
  st.dc = (int)(sum / (int64_t)n);
  double quietest = 1e18;
  const size_t win = audio::RATE / 20;
  for (size_t w = 0; w + win <= n; w += win) {
    double sq = 0;
    for (size_t i = w; i < w + win; i++) {
      int v = s[i] - st.dc;
      if (abs(v) > st.peak) st.peak = abs(v);
      sq += (double)v * v;
    }
    if (sq < quietest) quietest = sq;
  }
  double rms = sqrt(quietest / win);
  st.floorDb = rms > 0 ? 20.f * log10f((float)(rms / 32768.0)) : -96.f;
  return st;
}

static void micTest() {
  if (!audio::micOk()) { toast("NO MIC ADC FOUND"); return; }
  const size_t N = audio::RATE * 3;
  int16_t* l = (int16_t*)heap_caps_malloc(N * 2, MALLOC_CAP_SPIRAM);
  int16_t* r = (int16_t*)heap_caps_malloc(N * 2, MALLOC_CAP_SPIRAM);
  if (!l || !r) { heap_caps_free(l); heap_caps_free(r); toast("OUT OF MEMORY"); return; }
  banner("RECORDING", "3 SECONDS - TALK, THEN STAY QUIET");
  leds::setAll(60, 0, 0);
  leds::show();
  audio::record(l, r, 1600);  // drop what sat in the DMA buffers
  size_t got = audio::record(l, r, N);
  leds::setAll(0, 0, 0);
  leds::show();

  ChanStats sl = analyse(l, got), sr = analyse(r, got);
  // Average both mics when both are alive: their noise is uncorrelated, so it
  // partly cancels while the voice adds up. Otherwise use whichever works.
  const bool lOk = sl.peak > 300, rOk = sr.peak > 300;
  const char* mix = lOk && rOk ? "L+R" : rOk ? "R" : "L";
  // One-pole high-pass at ~90 Hz removes the DC offset and low rumble.
  const float a = 0.965f;
  float y = 0, prev = 0;
  int peak = 0;
  for (size_t i = 0; i < got; i++) {
    float x = lOk && rOk ? (l[i] + r[i]) * 0.5f : rOk ? r[i] : l[i];
    y = a * (y + x - prev);
    prev = x;
    int v = (int)y;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    l[i] = (int16_t)v;
    if (abs(v) > peak) peak = abs(v);
  }
  const MicGain& g = MIC_GAINS[s_micGain];
  snprintf(s_info[0], 48, "GAIN +%d/+%d DB  MIX %s  PEAK %d%%", g.analog, g.digital, mix, peak * 100 / 32768);
  snprintf(s_info[1], 48, "L PEAK %d%% FLOOR %.0f DB", sl.peak * 100 / 32768, sl.floorDb);
  snprintf(s_info[2], 48, "R PEAK %d%% FLOOR %.0f DB", sr.peak * 100 / 32768, sr.floorDb);
  char name[40];
  if (sdcard::nextName("MIC_", "WAV", name, sizeof(name))) writeWav(name, l, got);
  Serial.printf("[mic] %s | %s | %s | saved %s\n", s_info[0], s_info[1], s_info[2], name);
  banner("PLAYING BACK", s_info[0]);
  audio::play(l, got);
  heap_caps_free(l);
  heap_caps_free(r);
  s_infoUntil = millis() + 8000;
}

static void handle(BtnEvent e) {
  switch (e) {
    case BtnEvent::A: takePhoto(); break;
    case BtnEvent::B: {
      s_micGain = (s_micGain + 1) % MIC_GAIN_COUNT;
      prefs.putUChar("micgain", s_micGain);
      audio::setMicGain(MIC_GAINS[s_micGain].analog, MIC_GAINS[s_micGain].digital);
      char m[40];
      snprintf(m, sizeof(m), "MIC +%d / +%d DB", MIC_GAINS[s_micGain].analog, MIC_GAINS[s_micGain].digital);
      toast(m);
      break;
    }
    case BtnEvent::A_Long: micTest(); break;
    case BtnEvent::B_Long: {
      s_rot = (s_rot + 1) % display::ROTATION_COUNT;
      display::setRotation(s_rot);
      prefs.putUChar("rot", s_rot);
      toast(s_rot == 0 ? "USB-C ON THE LEFT" : "USB-C ON THE RIGHT");
      break;
    }
    case BtnEvent::AB: {
      s_preview = (s_preview + 1) % PREVIEW_COUNT;
      prefs.putUChar("preview", s_preview);
      banner("CAMERA RESTART", PREVIEWS[s_preview].name);
      bool ok = camera::start(PREVIEW_SIZE, 2);
      char m[40];
      snprintf(m, sizeof(m), "VIEW %s%s", PREVIEWS[s_preview].name, ok ? "" : " FAIL");
      Serial.printf("[camera] %s\n", m);
      toast(m, 2500);
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------- screen

static void overlay() {
  char line[64];
  uint32_t now = millis();
  cv.dim(0, 0, W, 34, 120);
  orientationMarks();
  snprintf(line, sizeof(line), "%.1f FPS", s_fps);
  cv.text(W - 6, 6, line, col::WHITE, 1, Align::Right);
  snprintf(line, sizeof(line), "VIEW %s  MIC +%d/+%d", PREVIEWS[s_preview].name, MIC_GAINS[s_micGain].analog, MIC_GAINS[s_micGain].digital);
  cv.text(W - 6, 18, line, col::DIM, 1, Align::Right);

  cv.dim(0, 178, W, 62, 110);
  if (sensors::accelOk()) snprintf(line, sizeof(line), "ACC %5d %5d %5d MG", s_ax, s_ay, s_az);
  else snprintf(line, sizeof(line), "ACC  NOT FOUND");
  cv.text(8, 182, line, col::CREAM, 1);
  if (s_lux >= 0) snprintf(line, sizeof(line), "LUX %.0f", s_lux);
  else snprintf(line, sizeof(line), "LUX  NOT FOUND");
  cv.text(W - 8, 182, line, col::CREAM, 1, Align::Right);

  bool a = buttons::downA(), b = buttons::downB();
  cv.fillRect(8, 194, 22, 12, a ? col::AMBER : rgb(40, 36, 32));
  cv.text(19, 197, "A", a ? col::BLACK : col::DIM, 1, Align::Center, false);
  cv.fillRect(34, 194, 22, 12, b ? col::AMBER : rgb(40, 36, 32));
  cv.text(45, 197, "B", b ? col::BLACK : col::DIM, 1, Align::Center, false);
  // The knob: its switch, and the raw encoder count (about 4 per click on a KY-040).
  const bool k = buttons::downKnob();
  cv.fillRect(60, 194, 22, 12, k ? col::AMBER : rgb(40, 36, 32));
  cv.text(71, 197, "K", k ? col::BLACK : col::DIM, 1, Align::Center, false);
  snprintf(line, sizeof(line), "%d", encoder::total());
  cv.text(88, 197, line, col::CREAM, 1);
  static uint32_t tot = 0, fr = 0, usageT = 0;
  if (usageT == 0 || now - usageT > 5000) { sdcard::usage(&tot, &fr); usageT = now | 1; }
  if (sdcard::mounted()) snprintf(line, sizeof(line), "SD %lu / %lu MB FREE", (unsigned long)fr, (unsigned long)tot);
  else snprintf(line, sizeof(line), "SD  NO CARD");
  cv.text(W - 8, 197, line, col::DIM, 1, Align::Right);

  cv.text(W / 2, 214, "A PHOTO " G_DOT " B MIC GAIN " G_DOT " HOLD A MIC", col::DIM, 1, Align::Center);
  cv.text(W / 2, 226, "HOLD B SCREEN " G_DOT " A+B VIEW SIZE", col::DIM, 1, Align::Center);

  if (now < s_infoUntil) {
    cv.fillRect(10, 110, 300, 50, rgb(12, 10, 8));
    cv.hline(10, 110, 300, col::AMBER);
    for (int i = 0; i < 3; i++) cv.text(W / 2, 116 + i * 14, s_info[i], i == 0 ? col::CREAM : col::DIM, 1, Align::Center, false);
  }
  if (now < s_toastUntil) {
    int w = Canvas::textWidth(s_toast, 2) + 16;
    cv.fillRect(W / 2 - w / 2, 44, w, 22, rgb(12, 10, 8));
    cv.hline(W / 2 - w / 2, 44, w, col::AMBER);
    cv.hline(W / 2 - w / 2, 65, w, col::AMBER);
    cv.text(W / 2, 48, s_toast, col::AMBER, 2, Align::Center, false);
  }
}

// ---------------------------------------------------------------- setup / loop

namespace bringup {

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n== Afterglow bring-up ==");
  Serial.printf("PSRAM %u KB free, internal %u KB free\n", (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  if (!cv.begin()) Serial.println("canvas allocation failed (no PSRAM?)");

  bool i2c = i2cbus::begin();
  int devs = i2c ? i2cbus::scan() : 0;
  set(C_I2C, i2c && devs ? 1 : 2, "%d DEVICES", devs);

  bool exp = xl9535::begin();
  set(C_EXP, exp ? 1 : 2, exp ? "0X20" : "NOT FOUND");

  prefs.begin("afterglow", false);
  s_rot = prefs.getUChar("rot", 0) % display::ROTATION_COUNT;
  s_orient = prefs.getUChar("orient", ORIENT_TURN) & 3;
  s_preview = prefs.getUChar("preview", camera::PREVIEW_DEFAULT) % PREVIEW_COUNT;
  s_micGain = prefs.getUChar("micgain", 2) % MIC_GAIN_COUNT;

  s_displayOk = display::begin(s_rot);
  set(C_DISP, s_displayOk ? 1 : 2, "ILI9341 R%d", s_rot);
  showChecklist();
  display::backlight(true);  // only now, so stale panel memory never shows

  camera::hardReset();
  bool cam = camera::start(PREVIEW_SIZE, 2);
  set(C_CAM, cam ? 1 : 2, cam ? "GC2145 %d MHZ" : "INIT FAILED", camera::xclk() / 1000000);
  showChecklist();

  bool sd = sdcard::begin();
  uint32_t tot, fr;
  sdcard::usage(&tot, &fr);
  set(C_SD, sd ? 1 : 3, sd ? "%lu MB FREE" : "NO CARD", (unsigned long)fr);
  showChecklist();

  bool aud = audio::begin();
  set(C_SPK, aud ? 1 : 2, aud ? "I2S 16 KHZ" : "I2S FAILED");
  set(C_MIC, audio::micOk() ? 1 : 3, audio::micOk() ? "ES7243E 0X%02X" : "NOT FOUND", audio::micAddr());
  audio::setMicGain(MIC_GAINS[s_micGain].analog, MIC_GAINS[s_micGain].digital);
  showChecklist();

  bool led = leds::begin();
  set(C_LED, led ? 1 : 2, led ? "GPIO46" : "RMT FAILED");

  sensors::begin();
  set(C_ACC, sensors::accelOk() ? 1 : 3, sensors::accelOk() ? "SC7A20H" : "NOT FOUND");
  set(C_ALS, sensors::lightOk() ? 1 : 3, sensors::lightOk() ? "LTR-303" : "NOT FOUND");

  bool btn = buttons::begin();
  set(C_BTN, btn ? 1 : 2, btn ? "PRESS ONE" : "NO EXPANDER");
  showChecklist();

  chime();
  delay(1200);
  s_fpsT0 = millis();
}

void loop() {
  BtnEvent e = buttons::poll();
  if (e != BtnEvent::None) {
    Serial.printf("[button] %s\n", buttons::name(e));
    if (checks[C_BTN].state == 1 && strcmp(checks[C_BTN].detail, "PRESS ONE") == 0) set(C_BTN, 1, "WORKING");
    handle(e);
  }

  uint32_t now = millis();
  if (now - s_sensT > 100) {
    s_sensT = now;
    if (sensors::accelOk()) sensors::accel(&s_ax, &s_ay, &s_az);
    if (sensors::lightOk()) s_lux = sensors::lux();
    uint8_t a = buttons::downA() ? 40 : 0, b = buttons::downB() ? 40 : 0;
    leds::set(0, a, a / 2, 0);
    leds::set(1, 0, 0, 0);
    leds::set(2, 0, b / 2, b);
    leds::show();
  }

  camera_fb_t* fb = camera::grab();
  if (fb) {
    if (now - s_camLogT > 3000) {
      s_camLogT = now;
      const uint16_t* p = (const uint16_t*)fb->buf;
      Serial.printf("[camera] %ux%u fmt %d len %u  px %04X %04X %04X %04X  %.1f fps\n", fb->width, fb->height,
                    fb->format, (unsigned)fb->len, p[0], p[1], p[fb->len / 4], p[fb->len / 2 - 1], s_fps);
    }
    copyFrame(fb);
    camera::release(fb);
  } else {
    testPattern();
    delay(30);
  }
  overlay();
  present();

  s_fpsFrames++;
  if (now - s_fpsT0 >= 1000) {
    s_fps = s_fpsFrames * 1000.f / (now - s_fpsT0);
    s_fpsFrames = 0;
    s_fpsT0 = now;
  }
}

}  // namespace bringup
