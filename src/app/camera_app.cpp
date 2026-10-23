#include "camera_app.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "img_converters.h"
#include "gallery.h"
#include "player.h"
#include "recorder.h"
#include "sound.h"
#include "transfer.h"
#include "../film/cine_fx.h"
#include "../film/film.h"
#include "../gfx/canvas.h"
#include "../gfx/font5x7.h"
#include "../gfx/rotate.h"
#include "../hal/audio.h"
#include "../hal/buttons.h"
#include "../hal/camera.h"
#include "../hal/clock.h"
#include "../hal/display.h"
#include "../hal/flash.h"
#include "../hal/i2c_bus.h"
#include "../hal/leds.h"
#include "../hal/sdcard.h"
#include "../hal/sensors.h"
#include "../hal/xl9535.h"
#include "../ui/cine.h"
#include "../ui/menus.h"
#include "../ui/screens.h"
#include "../ui/transfer_screen.h"
#include "../ui/viewfinder.h"

using namespace gfx;

// Flip this if the level line tilts the same way as the camera instead of
// staying with the horizon.
static constexpr float LEVEL_SIGN = 1.f;
static constexpr uint32_t WIND_MS = 520;
static constexpr int WIND_CLICKS = 5;  // thumbwheel clicks per frame
static constexpr uint32_t DEVELOP_MS = 2600;  // at least; longer if the save takes longer
static constexpr float EJECT_S = .9f, INSTANT_DEVELOP_S = 3.2f;
static constexpr uint32_t REVIEW_MS = 1800;
static constexpr uint32_t REWIND_MS = 2200;
static constexpr uint32_t LOADING_MS = 2500;
static constexpr uint32_t MODE_SWITCH_MS = 900;
static constexpr uint32_t TILT_STEP_MS = 420;
static constexpr float TILT_SCROLL_DEG = 12;
static constexpr float VOLUMES[] = {0, .25f, .5f, .75f, 1.f, 1.4f};
static constexpr int VOLUME_STEPS = sizeof(VOLUMES) / sizeof(VOLUMES[0]);
static constexpr int MAX_ITEMS = 999;
static constexpr int THUMB_SLOTS = 12;
static constexpr uint32_t CARTRIDGE_MS = 200000;   // a 50 ft Super 8 cartridge: 3 min 20 s at 18 fps
static constexpr uint32_t MAX_TAPE_CLIP_MS = 600000;  // camcorder clips stop at 10 minutes
static constexpr framesize_t CINE_SIZE = FRAMESIZE_QVGA;  // ~17 fps; cropped to 240x180

enum class Scene : uint8_t {
  View, Timer, Exposing, Darkroom, InstantDev, Review, Rewind, Loading,
  Menu, Shelf, Album, Photo, Play, Confirm, DateSet, ModeSwitch, RewindCrank, Transfer
};

static Canvas cv;
static Preferences prefs;

// Settings, saved
static uint8_t s_orient = ORIENT_TURN;
static uint8_t s_preview = camera::PREVIEW_DEFAULT;
static bool s_dateOn = true;
static uint8_t s_timer = 0;  // seconds, 0 = off
static bool s_flash = false;
static bool s_easyWind = false;
static uint8_t s_nextRollLen = 24;
static uint8_t s_volume = 4;
static bool s_perf = false;
static bool s_cine = false;           // cine mode
static ui::CineLook s_look = ui::CineLook::Super8;
static uint32_t s_cartridgeMs = 0;    // Super 8 footage used, saved
static bool s_videoLight = false;     // cine: keep the white LED on while filming
static uint32_t s_flashReadyAt = 0;   // the flash is charging until then
static int s_ev = 0;                  // exposure bias, -2..+2 half stops
static uint32_t s_cineFrame = 0;       // counts cine frames, for the CCTV stutter
static bool s_knobReversed = false;
static bool s_knobFast = false;       // 2 counts per click instead of 4

// The roll, saved so it survives power-off. For an instant pack, frame is the
// next shot (1..pack) and the counter shows the shots left.
static int s_stock = 0;
static bool s_noFilm = false;
static int s_rollLen = 24;
static int s_frame = 1;
static bool s_needWind = false;
static bool s_rollDone = false;

// Scene
static Scene s_scene = Scene::View;
static uint32_t s_sceneT0 = 0;
static uint32_t s_windT0 = 0;  // non-zero while the wind animation runs (B button, easy winding)
static int s_windClicks = 0;   // thumbwheel clicks toward the next frame
static int s_crankClicks = 0;  // rewind crank clicks so far
static uint32_t s_irisT0 = 0;
static int s_loadStock = 0;
static uint8_t s_loadSounds = 0;
static uint32_t s_nextBeep = 0;
static char s_toast[40] = "";
static uint32_t s_toastUntil = 0;
static uint32_t s_tiltStepT = 0;
static bool s_modeSwitched = false;

// Menus
static int s_menuSel = 0;
static int s_shelfSel = 0;
static float s_shelfOff = 0;
static bool s_shelfForced = false;
static Scene s_shelfBack = Scene::View;
static uint16_t* s_shelfPreview = nullptr;  // 112x84
static gallery::Item* s_items = nullptr;
static int s_itemCount = 0, s_albumSel = 0;
static float s_albumOff = 0;
static Scene s_albumBack = Scene::View;
static Scene s_confirmBack = Scene::Album;
static bool s_confirmDelete = false;  // which choice is highlighted
static struct tm s_dateEdit;
static int s_dateField = 0;

// Thumbnail cache for the album strip
static uint16_t* s_thumbPool = nullptr;
static uint16_t s_thumbKey[THUMB_SLOTS];
static int s_thumbNext = 0;

// Sensors
static float s_lux = -1, s_tilt = 0;
static bool s_levelValid = false;
static uint32_t s_sensT = 0, s_luxT = 0;

// The shot in progress, shared with the capture task
enum ShotState : uint8_t { SHOT_NONE, SHOT_EXPOSING, SHOT_DEVELOPED, SHOT_DONE, SHOT_FAILED };
static volatile uint8_t s_shot = SHOT_NONE;
static uint16_t* s_print = nullptr;  // 320x240: developed photo, album full view, clip first frame
static uint16_t* s_cineBuf = nullptr;  // 240x180 cine frame, what gets recorded
static int s_shotFrame = 0;
static bool s_shotInstant = false;
static char s_shotMsg[40] = "";

// Recording
static char s_clipPath[40] = "";
static uint32_t s_clipCartridgeStart = 0;

// Timing overlay
struct Perf { uint32_t rot, film, ui, lcd; float fps; };
static Perf s_perfNow = {};
static uint32_t s_fpsT0 = 0, s_fpsFrames = 0, s_perfLogT = 0;

static void toast(const char* m, uint32_t ms = 1500) {
  snprintf(s_toast, sizeof(s_toast), "%s", m);
  s_toastUntil = millis() + ms;
}

static void present() { display::push(cv.pixels()); }
static float ease(float p) { return p < .5f ? 2 * p * p : 1 - powf(-2 * p + 2, 2) / 2; }
static void go(Scene s) { s_scene = s; s_sceneT0 = millis(); }
static const film::Stock& rollStock() { return s_noFilm ? film::NO_FILM : film::STOCKS[s_stock]; }
static const film::Stock& cineStock() {
  switch (s_look) {
    case ui::CineLook::Camcorder: return film::CAMCORDER;
    case ui::CineLook::Silent: return film::SILENT;
    case ui::CineLook::Cctv: return film::CCTV;
    case ui::CineLook::Pocket: return film::POCKET;
    case ui::CineLook::Webcam: return film::WEBCAM;
    default: return film::SUPER8;
  }
}
static bool isPack() { return !s_noFilm && rollStock().pack > 0; }
static bool s_wideView = true;  // photo viewfinder matches the photo's field of view
// Cine: WIDE sees what a photo sees (~6 fps); SMOOTH is the zoomed-in 320x240
// window (about 40% of the view) at up to ~17 fps.
static bool s_cineWide = false;
static framesize_t viewSize() {
  if (s_cine) return CINE_SIZE;
  return s_wideView ? FRAMESIZE_QVGA : camera::PREVIEW_SIZES[s_preview].fs;
}
static bool startView() { return camera::start(viewSize(), 2, 0, s_cine ? s_cineWide : s_wideView); }

static void saveRoll() {
  prefs.putUChar("stock", (uint8_t)s_stock);
  prefs.putBool("nofilm", s_noFilm);
  prefs.putUChar("rolllen", (uint8_t)s_rollLen);
  prefs.putUChar("frame", (uint8_t)s_frame);
  prefs.putBool("needwind", s_needWind);
  prefs.putBool("rolldone", s_rollDone);
}

static void saveSettings() {
  prefs.putBool("date", s_dateOn);
  prefs.putUChar("timer", s_timer);
  prefs.putBool("flash", s_flash);
  prefs.putBool("easywind", s_easyWind);
  prefs.putUChar("nextroll", s_nextRollLen);
  prefs.putUChar("volume", s_volume);
  prefs.putBool("info", s_perf);
  prefs.putBool("cine", s_cine);
  prefs.putUChar("look", (uint8_t)s_look);
  prefs.putULong("cartridge", s_cartridgeMs);
  prefs.putBool("videolight", s_videoLight);
  prefs.putChar("ev", (int8_t)s_ev);
  prefs.putBool("wideview", s_wideView);
  prefs.putBool("cinewide", s_cineWide);
  prefs.putBool("knobrev", s_knobReversed);
  prefs.putBool("knobfast", s_knobFast);
}

static void applyKnob() { buttons::setKnob(s_knobReversed, s_knobFast ? 2 : 4); }

static void applyVolume() {
  audio::setVolume(VOLUMES[s_volume]);
  sound::setEnabled(s_volume > 0);
}

// ------------------------------------------------------------------ sensors

static void readSensors(uint32_t now) {
  if (now - s_sensT < 50) return;
  s_sensT = now;
  int16_t ax, ay, az;
  if (sensors::accelOk() && sensors::accel(&ax, &ay, &az)) {
    // In-plane angle of gravity from whichever board edge is down.
    const float ang = atan2f((float)ay, (float)ax) * 180.f / (float)M_PI;
    const float dev = ang - 90.f * roundf(ang / 90.f);
    s_levelValid = abs(az) < 700 && (ax * ax + ay * ay) > 400 * 400;
    s_tilt += (LEVEL_SIGN * dev - s_tilt) * .35f;
  }
  if (sensors::lightOk() && now - s_luxT > 250) {
    s_luxT = now;
    s_lux = sensors::lux();
  }
}

// Tilting the camera past 12 degrees scrolls lists hands-free. Returns -1, 0 or +1.
static int tiltStep(uint32_t now) {
  if (!s_levelValid || fabsf(s_tilt) < TILT_SCROLL_DEG || now - s_tiltStepT < TILT_STEP_MS) return 0;
  s_tiltStepT = now;
  return s_tilt > 0 ? 1 : -1;
}

// ------------------------------------------------------------------ capture task

// Brightness of a raw RGB565 sensor frame: mean luma 0..255 and the share of
// blown-out pixels, from a sparse grid (a few thousand samples).
static void frameStats(const camera_fb_t* fb, float* mean, float* clipped) {
  const uint16_t* px = (const uint16_t*)fb->buf;
  const int w = fb->width, h = fb->height, step = 16;
  uint32_t sum = 0, n = 0, hot = 0;
  for (int y = step / 2; y < h; y += step)
    for (int x = step / 2; x < w; x += step) {
      const uint16_t v = swap16(px[y * w + x]);
      const int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
      const int l = (r * 77 + g * 150 + b * 29) >> 8;
      sum += l;
      if (l >= 245) hot++;
      n++;
    }
  *mean = n ? (float)sum / n : 0;
  *clipped = n ? (float)hot / n : 0;
}

struct JpgSink { FILE* f; size_t bytes; };
static size_t jpgWrite(void* arg, size_t, const void* data, size_t len) {
  JpgSink* s = (JpgSink*)arg;
  s->bytes += len;
  return s->f ? fwrite(data, 1, len, s->f) : len;
}

static void shotFailed(const char* why) {
  snprintf(s_shotMsg, sizeof(s_shotMsg), "%s", why);
  startView();
  s_shot = SHOT_FAILED;
}

// Runs on core 0 while the main loop animates the shutter and the darkroom.
static void shotTask(void*) {
  const uint32_t t0 = millis();
  const bool useFlash = s_flash;
  // The flash stays lit for the whole exposure so auto exposure sees it.
  if (useFlash) flash::set(true);
  // A restart for the full-size frame resets the sensor, and its auto exposure
  // takes several slow frames to adapt (outdoors the photo came out white).
  // Instead: take the exposure the viewfinder settled on, convert it to the
  // photo mode's row time, set it by hand, then check the frame and correct.
  const camera::Exposure ex = camera::readExposure();
  const int viewWin = camera::windowWidth();
  camera::stop();
  bool started = false;
  for (framesize_t fs : camera::CAPTURE_SIZES)
    if ((started = camera::start(fs, 1))) break;
  camera_fb_t* fb = nullptr;
  if (started) {
    // Row time grows with the columns read, plus the fixed horizontal blank.
    constexpr int HBLANK = 331;
    float lines = ex.valid ? ex.lines * (float)(viewWin + HBLANK) / (camera::windowWidth() + HBLANK) : 300;
    uint8_t gain = ex.valid ? ex.gain : 0x55;
    const float target = 118.f * powf(2.f, camera::exposureBias() / 2.f);  // mean luma to aim for
    camera::manualExposure((uint16_t)lines, gain);
    camera::release(camera::grab());  // exposed before the new settings took effect
    for (int attempt = 0; attempt < 3; attempt++) {
      fb = camera::grab();
      if (!fb) break;
      float mean, clipped;
      frameStats(fb, &mean, &clipped);
      Serial.printf("[exposure] try %d: %u rows, gain 0x%02X -> mean %.0f, %.0f%% blown\n", attempt, (unsigned)lines, gain,
                    mean, clipped * 100);
      const bool tooBright = clipped > .08f || mean > target * 1.45f;
      const bool tooDark = mean < target * .45f && clipped < .01f;
      if (attempt == 2 || (!tooBright && !tooDark)) break;
      // Correct and try again. Blown-out pixels hide how bright it really is,
      // so a mostly white frame gets a big step down.
      float k = target / (mean > 1 ? mean : 1);
      if (clipped > .5f) k = .2f;
      else if (clipped > .2f) k = k < .4f ? k : .4f;
      if (k < .15f) k = .15f;
      if (k > 3.f) k = 3.f;
      lines *= k;
      if (lines < 1) {  // already at the shortest exposure: take gain down instead
        gain = (uint8_t)(gain * lines > 0x40 ? gain * lines : 0x40);
        lines = 1;
      }
      if (lines > 1200) lines = 1200;
      camera::release(fb);
      fb = nullptr;
      camera::manualExposure((uint16_t)lines, gain);
      camera::release(camera::grab());  // straddled the change
    }
  }
  sound::play(sound::Fx::ShutterClose);  // the exposure is over
  if (useFlash) {
    flash::set(false);
    sound::play(sound::Fx::FlashCharge);
    s_flashReadyAt = millis() + 2200;
  }
  const uint32_t tLag = millis() - t0;
  if (!fb) { flash::set(false); shotFailed("CAPTURE FAILED"); vTaskDelete(nullptr); return; }

  const film::Stock& st = film::current();
  int pw = cropWidth(fb->width, fb->height), ph = cropHeight(fb->width, fb->height);
  uint16_t* photo = (uint16_t*)heap_caps_malloc((size_t)pw * ph * 2, MALLOC_CAP_SPIRAM);
  if (photo) rotateCrop((const uint16_t*)fb->buf, fb->width, fb->height, photo, pw, ph, s_orient);
  camera::release(fb);
  if (!photo) { shotFailed("OUT OF MEMORY"); vTaskDelete(nullptr); return; }

  if (st.pack) {  // instant film is square: keep the middle of the frame
    const int side = ph, x0 = (pw - side) / 2;
    for (int y = 0; y < side; y++) memmove(photo + (size_t)y * side, photo + (size_t)y * pw + x0, side * 2);
    pw = side;
  }
  film::apply(photo, pw, ph, 1.15f, esp_random());
  if (s_dateOn && !st.pack) {
    Canvas pc;
    pc.wrap(photo, pw, ph);
    film::dateStamp(pc);
  }
  // A screen-sized copy for the darkroom, the review and the album.
  if (st.pack) {
    for (int i = 0; i < W * H; i++) s_print[i] = col::BLACK;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < H; x++) s_print[y * W + 40 + x] = photo[(size_t)(y * ph / H) * pw + x * pw / H];
  } else {
    scaleNearest(photo, pw, ph, s_print, W, H);
  }
  s_shot = SHOT_DEVELOPED;

  char name[40] = "";
  JpgSink sink = {nullptr, 0};
  if (sdcard::nextName("AFT_", "JPG", name, sizeof(name))) {
    sink.f = fopen(name, "wb");
    if (sink.f) setvbuf(sink.f, nullptr, _IOFBF, 16 * 1024);
  }
  const uint32_t tj = millis();
  const bool ok = fmt2jpg_cb((uint8_t*)photo, (size_t)pw * ph * 2, pw, ph, PIXFORMAT_RGB565, 90, jpgWrite, &sink);
  const bool saved = sink.f != nullptr;
  if (sink.f) fclose(sink.f);
  heap_caps_free(photo);
  if (ok && saved) {
    const char* us = strrchr(name, '_');
    gallery::Item it = {(uint16_t)(us ? atoi(us + 1) : 0), (uint8_t)s_stock, (uint8_t)s_shotFrame,
                        (uint8_t)(st.pack ? gallery::INSTANT : gallery::PHOTO), 0, (uint32_t)time(nullptr)};
    gallery::save(it, s_print);
  }
  Serial.printf("[photo] %s %dx%d %u KB  lag %lu  total %lu ms (jpeg+sd %lu)\n", saved ? name : "(no card)", pw, ph,
                (unsigned)(sink.bytes / 1024), (unsigned long)tLag, (unsigned long)(millis() - t0),
                (unsigned long)(millis() - tj));

  startView();
  if (!ok) { snprintf(s_shotMsg, sizeof(s_shotMsg), "JPEG FAILED"); s_shot = SHOT_FAILED; }
  else {
    snprintf(s_shotMsg, sizeof(s_shotMsg), saved ? "" : "NO CARD - NOT SAVED");
    s_shot = SHOT_DONE;
  }
  vTaskDelete(nullptr);
}

// ------------------------------------------------------------------ photo actions

static void shoot() {
  s_shotFrame = s_frame;
  s_shotInstant = isPack();
  if (s_shotInstant) {
    // Instant: nothing to wind; the pack counts down.
    s_frame++;
    s_needWind = false;
    s_rollDone = s_frame > s_rollLen;
  } else if (s_frame >= s_rollLen) {
    s_rollDone = true;
    s_needWind = false;
  } else {
    s_needWind = true;
  }
  saveRoll();
  sound::play(sound::Fx::ShutterOpen);
  s_shot = SHOT_EXPOSING;
  go(Scene::Exposing);
  xTaskCreatePinnedToCore(shotTask, "shot", 8192, nullptr, 2, nullptr, 0);
}

static void startWind(bool motor) {
  s_windT0 = millis();
  s_windClicks = 0;
  sound::play(motor ? sound::Fx::Motor : sound::Fx::Wind);
}

// One click of the thumbwheel: the film moves a fifth of a frame.
static void windClick() {
  if (!s_needWind || s_windT0) return;  // wound: the wheel is locked
  s_windClicks++;
  if (s_windClicks >= WIND_CLICKS) {
    s_windClicks = 0;
    s_frame++;
    s_needWind = false;
    saveRoll();
    sound::play(sound::Fx::WindStop);
  } else {
    sound::play(sound::Fx::Ratchet);
  }
}

static void startCrank() {
  s_crankClicks = 0;
  go(Scene::RewindCrank);
}

static void startLoading(int stock) {
  s_loadStock = (stock + film::STOCK_COUNT) % film::STOCK_COUNT;
  s_loadSounds = 0;
  sound::play(sound::Fx::Clunk);
  go(Scene::Loading);
}

static void finishLoading() {
  s_stock = s_loadStock;
  s_noFilm = false;
  film::select(film::STOCKS[s_stock]);
  s_rollLen = film::STOCKS[s_stock].pack ? film::STOCKS[s_stock].pack : s_nextRollLen;
  s_frame = 1;
  s_needWind = false;
  s_rollDone = false;
  saveRoll();
  go(Scene::View);
  toast(film::STOCKS[s_stock].pack ? "PACK LOADED" : "FRAME 01");
}

static void unload() {
  // The finished roll or empty pack leaves the camera.
  s_noFilm = true;
  s_rollDone = false;
  s_needWind = false;
  s_frame = 1;
  saveRoll();
  film::select(film::NO_FILM);
}

static void openShelf(bool forced, Scene back) {
  s_shelfForced = forced;
  s_shelfBack = back;
  s_shelfSel = s_noFilm ? 0 : s_stock;
  s_shelfOff = (float)s_shelfSel;
  film::select(film::STOCKS[s_shelfSel]);  // the preview shows the highlighted film
  go(Scene::Shelf);
}

static void closeShelf() {
  film::select(rollStock());  // back to what is actually loaded
  go(s_shelfForced ? Scene::View : s_shelfBack);
}

// ------------------------------------------------------------------ cine actions

static float cartridgeFeet() {
  const uint32_t ms = s_cartridgeMs + (recorder::recording() ? recorder::elapsedMs() : 0);
  return ms / 4000.f;  // 18 frames/s, 72 frames per foot: 4 s of film per foot
}

static void startRecording() {
  if (!sdcard::mounted()) { sound::play(sound::Fx::Thud); toast("NO CARD"); return; }
  if (s_look == ui::CineLook::Super8 && s_cartridgeMs >= CARTRIDGE_MS) {
    s_cartridgeMs = 0;  // a fresh cartridge
    saveSettings();
    toast("NEW CARTRIDGE");
  }
  recorder::Options opt;
  if (s_look == ui::CineLook::Silent) { opt.audio = false; opt.speed = 1.4f; }  // silent, and undercranked
  if (s_look == ui::CineLook::Webcam) opt.quality = 30;                        // real compression artefacts
  if (s_look == ui::CineLook::Pocket) opt.quality = 70;
  if (!sdcard::nextName("AFT_", "AVI", s_clipPath, sizeof(s_clipPath)) || !recorder::start(s_clipPath, opt)) {
    sound::play(sound::Fx::Thud);
    toast("CAN'T RECORD");
    return;
  }
  sound::play(sound::Fx::Clunk);
  s_clipCartridgeStart = s_cartridgeMs;
  // The clip's album picture: the first frame without the on-screen display.
  scaleNearest(s_cineBuf, recorder::W, recorder::H, s_print, W, H);
}

static void stopRecording(const char* why = nullptr) {
  const recorder::Result r = recorder::stop();
  sound::play(sound::Fx::Clunk);
  if (s_look == ui::CineLook::Super8) {
    s_cartridgeMs = s_clipCartridgeStart + r.durationMs;
    saveSettings();
  }
  if (!r.ok) { toast("CLIP FAILED", 2500); return; }
  const char* us = strrchr(s_clipPath, '_');
  gallery::Item it = {(uint16_t)(us ? atoi(us + 1) : 0), (uint8_t)s_look, 0, gallery::CLIP,
                      (uint16_t)(r.durationMs / 100), (uint32_t)time(nullptr)};
  gallery::save(it, s_print);
  toast(why ? why : "CLIP SAVED", 2000);
}

static void nextLook(int dir) {
  s_look = (ui::CineLook)(((int)s_look + dir + ui::CINE_LOOK_COUNT) % ui::CINE_LOOK_COUNT);
  film::select(cineStock());
  saveSettings();
}

static void startModeSwitch() {
  if (recorder::recording()) stopRecording();
  s_modeSwitched = false;
  sound::play(sound::Fx::Clunk);
  go(Scene::ModeSwitch);
}

// ------------------------------------------------------------------ album

static void openAlbum(Scene back) {
  if (recorder::recording()) stopRecording();
  s_albumBack = back;
  s_itemCount = s_items ? gallery::list(s_items, MAX_ITEMS) : 0;
  s_albumSel = 0;
  s_albumOff = 0;
  go(Scene::Album);
}

static const uint16_t* thumbFor(uint16_t num) {
  for (int i = 0; i < THUMB_SLOTS; i++)
    if (s_thumbKey[i] == num) return s_thumbPool + i * gallery::THUMB_W * gallery::THUMB_H;
  const int slot = s_thumbNext;
  s_thumbNext = (s_thumbNext + 1) % THUMB_SLOTS;
  uint16_t* dst = s_thumbPool + slot * gallery::THUMB_W * gallery::THUMB_H;
  if (!gallery::loadThumb(num, dst)) { s_thumbKey[slot] = 0xFFFF; return nullptr; }
  s_thumbKey[slot] = num;
  return dst;
}

static void openItem() {
  if (!s_itemCount) return;
  const gallery::Item& it = s_items[s_albumSel];
  if (it.kind == gallery::CLIP) {
    char p[48];
    gallery::mediaPath(it, p, sizeof(p));
    if (!player::open(p)) { toast("CAN'T PLAY CLIP"); return; }
    for (int i = 0; i < W * H; i++) s_print[i] = col::BLACK;
    go(Scene::Play);
    return;
  }
  if (!gallery::loadPreview(it.num, s_print)) { toast("CAN'T READ PHOTO"); return; }
  go(Scene::Photo);
}

static void deleteSelected() {
  const gallery::Item it = s_items[s_albumSel];
  const bool ok = gallery::remove(it);
  for (auto& k : s_thumbKey) if (k == it.num) k = 0xFFFF;
  s_itemCount = gallery::list(s_items, MAX_ITEMS);
  if (s_albumSel >= s_itemCount) s_albumSel = s_itemCount ? s_itemCount - 1 : 0;
  s_albumOff = (float)s_albumSel;
  sound::play(sound::Fx::Thud);
  toast(ok ? "DELETED" : "DELETE FAILED");
  go(Scene::Album);
}

// ------------------------------------------------------------------ Wi-Fi transfer

static ui::Qr s_qrJoin, s_qrOpen;
static bool s_qrShowJoin = true;
static uint32_t s_qrFlipT = 0, s_transferDrawT = 0;

static void enterTransfer() {
  if (recorder::recording()) stopRecording();
  // The camera pauses: Wi-Fi needs its memory, including the film table.
  camera::stop();
  film::release();
  // A password made once per camera and kept, so the phone remembers the network.
  String pass = prefs.getString("wifipass", "");
  if (pass.length() < 8) {
    char p[12];
    snprintf(p, sizeof(p), "%08lu", (unsigned long)(esp_random() % 100000000UL));
    pass = p;
    prefs.putString("wifipass", pass);
  }
  if (!transfer::start(pass.c_str())) {
    film::select(s_cine ? cineStock() : rollStock());
    startView();
    toast("WI-FI FAILED", 2500);
    go(Scene::View);
    return;
  }
  const transfer::Status& st = transfer::status();
  char text[96];
  // Standard Wi-Fi QR format: phone cameras offer to join straight away.
  snprintf(text, sizeof(text), "WIFI:T:WPA;S:%s;P:%s;;", st.ssid, st.pass);
  ui::makeQr(text, &s_qrJoin);
  snprintf(text, sizeof(text), "http://%s/", st.url);
  ui::makeQr(text, &s_qrOpen);
  s_qrShowJoin = true;
  s_qrFlipT = millis();
  sound::play(sound::Fx::Chime);
  Serial.printf("[wifi] %s  password %s  http://%s/  internal free %u KB\n", st.ssid, st.pass, st.url,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  go(Scene::Transfer);
}

static void exitTransfer() {
  const bool synced = transfer::status().timeSynced;
  transfer::stop();
  film::select(s_cine ? cineStock() : rollStock());
  startView();
  Serial.printf("[wifi] off; film table in %s RAM\n", film::tableInternal() ? "internal" : "PS");
  sound::play(sound::Fx::Clunk);
  toast(synced ? "CLOCK SET FROM PHONE" : "WI-FI OFF", 2500);
  go(Scene::View);
}

// ------------------------------------------------------------------ settings menu

enum MenuItem : uint8_t {
  M_FILM, M_LOOK, M_DATE, M_TIMER, M_FLASH, M_WIND, M_ROLL, M_VOLUME, M_SETDATE, M_KNOB, M_KNOBSPEED, M_INFO,
  M_ALBUM, M_CLOSE, M_LIGHT, M_EV, M_VIEW, M_WIFI
};
static const MenuItem PHOTO_MENU[] = {M_FILM, M_EV, M_VIEW, M_DATE, M_TIMER, M_FLASH, M_WIND, M_ROLL, M_VOLUME,
                                      M_SETDATE, M_KNOB, M_KNOBSPEED, M_INFO, M_ALBUM, M_WIFI, M_CLOSE};
static const MenuItem CINE_MENU[] = {M_LOOK, M_VIEW, M_EV, M_LIGHT, M_DATE, M_VOLUME, M_SETDATE, M_KNOB, M_KNOBSPEED, M_ALBUM, M_WIFI, M_CLOSE};

static int menuItems(const MenuItem** out) {
  *out = s_cine ? CINE_MENU : PHOTO_MENU;
  return s_cine ? sizeof(CINE_MENU) / sizeof(CINE_MENU[0]) : sizeof(PHOTO_MENU) / sizeof(PHOTO_MENU[0]);
}

static void menuRow(MenuItem m, ui::MenuRow& r) {
  static const char* labels[] = {"FILM", "LOOK", "DATE STAMP", "SELF TIMER", "FLASH", "WINDING", "ROLL LENGTH",
                                 "VOLUME", "SET DATE", "KNOB", "KNOB SPEED", "INFO", "ALBUM", "CLOSE", "VIDEO LIGHT",
                                 "EXPOSURE", "VIEWFINDER", "WI-FI TRANSFER"};
  r.label = labels[m];
  r.value[0] = 0;
  switch (m) {
    case M_FILM: snprintf(r.value, 24, "%s", s_noFilm ? "NONE" : film::STOCKS[s_stock].name); break;
    case M_LOOK: snprintf(r.value, 24, "%s", cineStock().name); break;
    case M_DATE: snprintf(r.value, 24, "%s", s_dateOn ? "ON" : "OFF"); break;
    case M_TIMER:
      if (s_timer) snprintf(r.value, 24, "%dS", s_timer);
      else snprintf(r.value, 24, "OFF");
      break;
    case M_FLASH: snprintf(r.value, 24, "%s", s_flash ? "ON" : "OFF"); break;
    case M_WIND: snprintf(r.value, 24, "%s", s_easyWind ? "EASY" : "MANUAL"); break;
    case M_ROLL: snprintf(r.value, 24, "%d EXP", s_nextRollLen); break;
    case M_VOLUME:
      if (s_volume) {
        char* v = r.value;  // a little level bar
        for (int i = 1; i < VOLUME_STEPS; i++) *v++ = i <= s_volume ? '#' : '-';
        *v = 0;
      } else {
        snprintf(r.value, 24, "OFF");
      }
      break;
    case M_SETDATE: {
      char d[16];
      film::dateText(d, sizeof(d));
      snprintf(r.value, 24, "%s " G_RIGHT, d);
      break;
    }
    case M_LIGHT: snprintf(r.value, 24, "%s", s_videoLight ? "ON" : "OFF"); break;
    case M_VIEW:
      if (s_cine) snprintf(r.value, 24, "%s", s_cineWide ? "WIDE" : "SMOOTH");
      else snprintf(r.value, 24, "%s", s_wideView ? "WIDE" : "SHARP");
      break;
    case M_EV:
      // Half-stop steps, shown the way a compensation dial reads.
      if (s_ev == 0) snprintf(r.value, 24, "0");
      else snprintf(r.value, 24, "%s%d.%d", s_ev > 0 ? "+" : "-", abs(s_ev) / 2, abs(s_ev) % 2 ? 5 : 0);
      break;
    case M_KNOB: snprintf(r.value, 24, "%s", s_knobReversed ? "REVERSED" : "NORMAL"); break;
    case M_KNOBSPEED: snprintf(r.value, 24, "%s", s_knobFast ? "2X" : "1X"); break;
    case M_INFO: snprintf(r.value, 24, "%s", s_perf ? "ON" : "OFF"); break;
    case M_ALBUM: snprintf(r.value, 24, G_RIGHT); break;
    case M_WIFI: snprintf(r.value, 24, G_RIGHT); break;
    case M_CLOSE: break;
  }
}

static void menuChange(MenuItem m) {
  switch (m) {
    case M_FILM: openShelf(false, Scene::Menu); return;
    case M_LOOK: nextLook(1); return;
    case M_VIEW:
      // Photo: WIDE shows exactly what the photo gets; SHARP is the 640x480 view (80% of it).
      // Cine: WIDE matches the photo's view; SMOOTH is zoomed in but faster.
      if (s_cine) s_cineWide = !s_cineWide;
      else s_wideView = !s_wideView;
      startView();
      break;
    case M_EV:
      s_ev = s_ev >= 2 ? -2 : s_ev + 1;
      camera::setExposureBias(s_ev);
      break;
    case M_DATE: s_dateOn = !s_dateOn; break;
    case M_TIMER: s_timer = s_timer == 0 ? 3 : s_timer == 3 ? 10 : 0; break;
    case M_FLASH: s_flash = !s_flash; break;
    case M_WIND: s_easyWind = !s_easyWind; break;
    case M_ROLL:
      s_nextRollLen = s_nextRollLen == 12 ? 24 : s_nextRollLen == 24 ? 36 : 12;
      toast("APPLIES TO NEXT ROLL");
      break;
    case M_VOLUME:
      s_volume = (s_volume + 1) % VOLUME_STEPS;
      applyVolume();
      break;
    case M_SETDATE: {
      time_t now = time(nullptr);
      localtime_r(&now, &s_dateEdit);
      s_dateField = 0;
      go(Scene::DateSet);
      return;
    }
    case M_LIGHT: s_videoLight = !s_videoLight; break;
    case M_KNOB: s_knobReversed = !s_knobReversed; applyKnob(); break;
    case M_KNOBSPEED: s_knobFast = !s_knobFast; applyKnob(); break;
    case M_INFO: s_perf = !s_perf; break;
    case M_ALBUM: openAlbum(Scene::Menu); return;
    case M_WIFI: enterTransfer(); return;
    case M_CLOSE: go(Scene::View); return;
  }
  saveSettings();
}

// ------------------------------------------------------------------ date back

static int daysIn(int year, int month) {  // month 0..11
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 1 && leap ? 29 : d[month];
}

static int wrap(int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; }

static void dateStep(int dir) {
  struct tm& t = s_dateEdit;
  switch (s_dateField) {
    case 0: t.tm_year = wrap(t.tm_year + dir, 2024 - 1900, 2099 - 1900); break;
    case 1: t.tm_mon = wrap(t.tm_mon + dir, 0, 11); break;
    case 2: t.tm_mday = wrap(t.tm_mday + dir, 1, daysIn(t.tm_year + 1900, t.tm_mon)); break;
    case 3: t.tm_hour = wrap(t.tm_hour + dir, 0, 23); break;
    case 4: t.tm_min = wrap(t.tm_min + dir, 0, 59); break;
  }
  const int dim = daysIn(t.tm_year + 1900, t.tm_mon);
  if (t.tm_mday > dim) t.tm_mday = dim;
}

// ------------------------------------------------------------------ viewfinder input

static void handlePhotoView(BtnEvent e) {
  switch (e) {
    case BtnEvent::TurnCW:
      windClick();
      return;
    case BtnEvent::TurnCCW:
      // At the end of a roll, turning back cranks the rewind.
      if (s_rollDone && !isPack() && !s_noFilm) { startCrank(); s_crankClicks = 1; sound::play(sound::Fx::Crank); }
      return;
    case BtnEvent::Knob:
      // With nothing to shoot, the knob press loads film instead.
      if (s_noFilm) { openShelf(true, Scene::View); return; }
      if (s_rollDone && isPack()) {
        sound::play(sound::Fx::Clunk);
        unload();
        openShelf(true, Scene::View);
        return;
      }
      [[fallthrough]];
    case BtnEvent::A:
      if (s_windT0) { sound::play(sound::Fx::Thud); return; }
      if (s_noFilm) { sound::play(sound::Fx::Thud); toast("NO FILM"); return; }
      if (s_rollDone) { sound::play(sound::Fx::Thud); toast(isPack() ? "PACK EMPTY" : "END OF ROLL"); return; }
      if (s_needWind) { sound::play(sound::Fx::Thud); toast("WIND FIRST"); return; }
      if (s_flash && millis() < s_flashReadyAt) { sound::play(sound::Fx::Thud); toast("FLASH CHARGING"); return; }
      if (s_timer) {
        s_nextBeep = millis();
        go(Scene::Timer);
        return;
      }
      shoot();
      return;
    case BtnEvent::B:
      if (s_windT0) return;
      if (s_noFilm) { openShelf(true, Scene::View); return; }
      if (s_rollDone) {
        if (isPack()) {  // the empty pack drops out; choose the next one
          sound::play(sound::Fx::Clunk);
          unload();
          openShelf(true, Scene::View);
          return;
        }
        sound::play(sound::Fx::MotorLong);
        go(Scene::Rewind);
        return;
      }
      if (isPack()) { sound::play(sound::Fx::Tick); toast("INSTANT: NO WINDING"); return; }
      if (s_needWind) { startWind(false); return; }
      sound::play(sound::Fx::Thud);
      toast("ALREADY WOUND");
      return;
    default: return;
  }
}

static void handleCineView(BtnEvent e) {
  switch (e) {
    case BtnEvent::A:
    case BtnEvent::Knob:
      if (recorder::recording()) stopRecording();
      else startRecording();
      return;
    case BtnEvent::TurnCW:
    case BtnEvent::TurnCCW:
      if (recorder::recording()) return;
      nextLook(e == BtnEvent::TurnCW ? 1 : -1);
      sound::play(sound::Fx::Clunk);
      toast(cineStock().name);
      return;
    case BtnEvent::B:
      if (recorder::recording()) { sound::play(sound::Fx::Thud); toast("STOP RECORDING FIRST"); return; }
      nextLook(1);
      sound::play(sound::Fx::Clunk);
      toast(cineStock().name);
      return;
    default: return;
  }
}

static void handleView(BtnEvent e) {
  switch (e) {
    case BtnEvent::KnobHold:
    case BtnEvent::B_Long:
      if (recorder::recording()) { sound::play(sound::Fx::Thud); toast("STOP RECORDING FIRST"); return; }
      s_menuSel = 0;
      sound::play(sound::Fx::Tick);
      go(Scene::Menu);
      return;
    case BtnEvent::A_Long:
      openAlbum(Scene::View);
      return;
    case BtnEvent::AB:
    case BtnEvent::PushTurnCW:
    case BtnEvent::PushTurnCCW:
      startModeSwitch();
      return;
    default:
      if (s_cine) handleCineView(e);
      else handlePhotoView(e);
  }
}

// ------------------------------------------------------------------ drawing

static void drawToast(uint32_t now) {
  if (now >= s_toastUntil) return;
  const int w = Canvas::textWidth(s_toast, 2) + 16, x = W / 2 - w / 2;
  cv.fillRect(x, 34, w, 22, rgb(12, 10, 8));
  cv.hline(x, 34, w, col::AMBER);
  cv.hline(x, 55, w, col::AMBER);
  cv.text(W / 2, 38, s_toast, col::AMBER, 2, Align::Center, false);
}

static void drawIris(uint32_t now) {
  if (!s_irisT0 || now - s_irisT0 > 500) return;
  const int r = (int)(ease((now - s_irisT0) / 500.f) * 210), r2 = r * r;
  uint16_t* px = cv.pixels();
  for (int y = 0; y < H; y++) {
    const int dy = y - 120;
    for (int x = 0; x < W; x++) {
      const int dx = x - 160;
      if (dx * dx + dy * dy > r2) px[y * W + x] = col::BLACK;
    }
  }
}

static void drawPerf() {
  char l[64];
  snprintf(l, sizeof(l), "%.1f FPS  ROT %lu FILM %lu UI %lu LCD %lu", s_perfNow.fps, (unsigned long)s_perfNow.rot,
           (unsigned long)s_perfNow.film, (unsigned long)s_perfNow.ui, (unsigned long)s_perfNow.lcd);
  cv.fillRect(0, 170, W, 11, col::BLACK);
  cv.text(4, 172, l, col::GREEN, 1, Align::Left, false);
}

static uint32_t s_ledKey = 0xFFFFFFFF;
static void updateLeds(uint32_t now) {
  uint8_t r = 0, g = 0, b = 0;
  if (s_scene == Scene::Exposing && s_flash) {
    r = g = b = 255;  // the flash: all three LEDs at full white during the exposure
  } else if (s_scene == Scene::Timer) {
    const int rem = s_timer * 1000 - (int)(now - s_sceneT0), per = rem < 2000 ? 250 : 1000;
    if (((now - s_sceneT0) / (per / 2)) % 2 == 0) r = 80;
  } else if (s_scene == Scene::Darkroom || s_scene == Scene::InstantDev) {
    const float a = .35f + .65f * fabsf(sinf(now / 400.f));  // amber glow while developing
    r = (uint8_t)(50 * a);
    g = (uint8_t)(24 * a);
  } else if (s_scene == Scene::Rewind || s_scene == Scene::Loading) {
    if ((now / 200) % 2 == 0) { r = 40; g = 20; }
  } else if (recorder::recording()) {
    r = 60;  // the tally light
  } else if (s_scene == Scene::Transfer) {
    b = (uint8_t)(10 + 40 * fabsf(sinf(now / 900.f)));  // Wi-Fi on
  }
  const uint32_t key = (r << 16) | (g << 8) | b;
  if (key == s_ledKey) return;
  s_ledKey = key;
  leds::setAll(r, g, b);
  leds::show();
}

// Grabs a photo-mode frame and draws it through the current film.
static bool renderLive() {
  camera_fb_t* fb = camera::grab();
  if (!fb) return false;
  const uint32_t t0 = millis();
  rotateCrop((const uint16_t*)fb->buf, fb->width, fb->height, cv.pixels(), W, H, s_orient);
  camera::release(fb);
  const uint32_t t1 = millis();
  film::apply(cv.pixels(), W, H, 1.f, esp_random());
  s_perfNow.rot = t1 - t0;
  s_perfNow.film = millis() - t1;
  return true;
}

// Grabs a cine frame: 240x180 with the look and effects baked in (this is
// what gets recorded), then scaled up to the screen.
static bool renderCine(uint32_t now) {
  camera_fb_t* fb = camera::grab();
  if (!fb) return false;
  const uint32_t t0 = millis();
  rotateCrop((const uint16_t*)fb->buf, fb->width, fb->height, s_cineBuf, recorder::W, recorder::H, s_orient);
  camera::release(fb);
  const uint32_t t1 = millis();
  film::apply(s_cineBuf, recorder::W, recorder::H, 1.f, esp_random());
  const int fw = recorder::W, fh = recorder::H;
  switch (s_look) {
    case ui::CineLook::Super8: cinefx::super8(s_cineBuf, fw, fh); break;
    case ui::CineLook::Camcorder:
      cinefx::camcorder(s_cineBuf, fw, fh, now);
      if (s_dateOn) cinefx::burnCamcorderDate(s_cineBuf, fw, fh);
      break;
    case ui::CineLook::Silent: cinefx::silent(s_cineBuf, fw, fh); break;
    case ui::CineLook::Cctv: cinefx::cctv(s_cineBuf, fw, fh); break;
    case ui::CineLook::Pocket: cinefx::pocket(s_cineBuf, fw, fh); break;
    case ui::CineLook::Webcam: cinefx::webcam(s_cineBuf, fw, fh); break;
  }
  // A security camera records jerkily: only every other frame.
  if (s_look != ui::CineLook::Cctv || (s_cineFrame & 1) == 0) recorder::pushFrame(s_cineBuf);
  s_cineFrame++;
  scaleNearest(s_cineBuf, recorder::W, recorder::H, cv.pixels(), W, H);
  s_perfNow.rot = t1 - t0;
  s_perfNow.film = millis() - t1;
  return true;
}

static void noCamera() {
  cv.fill(rgb(20, 20, 24));
  cv.text(W / 2, 110, "NO CAMERA", col::RED, 2, Align::Center, false);
}

static void drawPhotoOverlay(uint32_t now) {
  float counterOffset = 0, windP = 0;
  if (s_windT0) {
    const float p = (now - s_windT0) / (float)WIND_MS;
    if (p >= 1) {
      s_windT0 = 0;
      s_frame++;
      s_needWind = false;
      saveRoll();
    } else {
      windP = p;
      counterOffset = ease(p);
    }
  } else if (s_windClicks) {
    // Part-way through winding by hand: the film stays where the thumb left it.
    windP = counterOffset = (float)s_windClicks / WIND_CLICKS;
  }
  ui::ViewState vs = {};
  vs.stock = &rollStock();
  vs.noFilm = s_noFilm;
  const bool pack = isPack();
  vs.frame = pack ? s_rollLen - s_frame + 1 : s_frame;  // instant: shots left
  vs.rollLen = s_rollLen;
  vs.countDown = pack;
  vs.flashOn = s_flash;
  vs.flashCharging = s_flash && now < s_flashReadyAt;
  vs.counterOffset = counterOffset;
  vs.dateOn = s_dateOn;
  vs.lux = s_lux;
  vs.tiltDeg = s_tilt;
  vs.levelValid = s_levelValid;
  vs.needWind = s_needWind;
  vs.rollDone = s_rollDone;
  vs.windP = windP;
  vs.nowMs = now;
  ui::drawViewfinder(cv, vs);
}

static void viewScene(uint32_t now) {
  if (s_cine) {
    // Clips stop by themselves at the end of a cartridge or tape.
    if (recorder::recording()) {
      if (s_look == ui::CineLook::Super8 && s_clipCartridgeStart + recorder::elapsedMs() >= CARTRIDGE_MS)
        stopRecording("CARTRIDGE FULL");
      else if (recorder::elapsedMs() >= MAX_TAPE_CLIP_MS)
        stopRecording("CLIP SAVED");
    }
    if (!renderCine(now)) { noCamera(); present(); delay(100); return; }
  } else if (!renderLive()) {
    noCamera();
    present();
    delay(100);
    return;
  }
  const uint32_t t2 = millis();
  if (s_cine) ui::drawCineOsd(cv, s_look, recorder::recording(), recorder::elapsedMs(), cartridgeFeet(), now);
  else drawPhotoOverlay(now);
  drawToast(now);
  drawIris(now);
  if (s_perf) drawPerf();
  const uint32_t t3 = millis();
  present();
  s_perfNow.ui = t3 - t2;
  s_perfNow.lcd = millis() - t3;
  s_fpsFrames++;
  if (now - s_fpsT0 >= 1000) {
    s_perfNow.fps = s_fpsFrames * 1000.f / (now - s_fpsT0);
    s_fpsFrames = 0;
    s_fpsT0 = now;
  }
  if (now - s_perfLogT > 10000) {
    s_perfLogT = now;
    Serial.printf("[perf] %s %.1f fps  rotate %lu  look %lu  ui %lu  lcd %lu ms%s\n", s_cine ? "cine" : "photo",
                  s_perfNow.fps, (unsigned long)s_perfNow.rot, (unsigned long)s_perfNow.film,
                  (unsigned long)s_perfNow.ui, (unsigned long)s_perfNow.lcd,
                  recorder::recording() ? "  (recording)" : "");
  }
}

// ------------------------------------------------------------------ setup / loop

namespace cameraApp {

void setup() {
  Serial.begin(115200);
  cv.begin();
  s_print = (uint16_t*)heap_caps_malloc(W * H * 2, MALLOC_CAP_SPIRAM);
  s_cineBuf = (uint16_t*)heap_caps_malloc(recorder::W * recorder::H * 2, MALLOC_CAP_SPIRAM);
  s_shelfPreview = (uint16_t*)heap_caps_malloc(112 * 84 * 2, MALLOC_CAP_SPIRAM);
  s_items = (gallery::Item*)heap_caps_malloc(MAX_ITEMS * sizeof(gallery::Item), MALLOC_CAP_SPIRAM);
  s_thumbPool = (uint16_t*)heap_caps_malloc(THUMB_SLOTS * gallery::THUMB_W * gallery::THUMB_H * 2, MALLOC_CAP_SPIRAM);
  for (auto& k : s_thumbKey) k = 0xFFFF;
  i2cbus::begin();
  xl9535::begin();
  prefs.begin("afterglow", false);
  s_orient = prefs.getUChar("orient", ORIENT_TURN) & 3;
  s_preview = prefs.getUChar("preview", camera::PREVIEW_DEFAULT) % camera::PREVIEW_COUNT;
  s_dateOn = prefs.getBool("date", true);
  s_timer = prefs.getUChar("timer", 0);
  s_flash = prefs.getBool("flash", false);
  s_easyWind = prefs.getBool("easywind", false);
  s_nextRollLen = prefs.getUChar("nextroll", 24);
  s_volume = prefs.getUChar("volume", 4) % VOLUME_STEPS;
  s_perf = prefs.getBool("info", false);
  s_cine = prefs.getBool("cine", false);
  s_look = (ui::CineLook)(prefs.getUChar("look", 0) % ui::CINE_LOOK_COUNT);
  s_ev = prefs.getChar("ev", 0);
  s_wideView = prefs.getBool("wideview", true);
  s_cineWide = prefs.getBool("cinewide", false);
  camera::setExposureBias(s_ev);
  s_cartridgeMs = prefs.getULong("cartridge", 0);
  s_videoLight = prefs.getBool("videolight", false);
  s_knobReversed = prefs.getBool("knobrev", false);
  s_knobFast = prefs.getBool("knobfast", false);
  s_stock = prefs.getUChar("stock", 0) % film::STOCK_COUNT;
  s_noFilm = prefs.getBool("nofilm", false);
  s_rollLen = prefs.getUChar("rolllen", 24);
  s_frame = prefs.getUChar("frame", 1);
  if (s_frame < 1 || s_frame > s_rollLen + 1) s_frame = 1;
  s_needWind = prefs.getBool("needwind", false);
  s_rollDone = prefs.getBool("rolldone", false);

  display::begin(prefs.getUChar("rot", 0) % display::ROTATION_COUNT);
  ui::drawBoot(cv, 0, nullptr);
  present();
  display::backlight(true);
  const uint32_t bootT0 = millis();

  clock_::begin();
  camera::hardReset();
  startView();
  sdcard::begin();
  audio::begin();
  sound::begin();
  applyVolume();
  leds::begin();
  flash::begin();
  sensors::begin();
  buttons::begin();
  applyKnob();
  film::select(s_cine ? cineStock() : rollStock());
  Serial.printf("[boot] ready in %lu ms, film table in %s RAM, PSRAM free %u KB, internal free %u KB\n",
                (unsigned long)(millis() - bootT0), film::tableInternal() ? "internal" : "PS",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

  char status[48];
  if (s_cine) snprintf(status, sizeof(status), "CINE  " G_DOT "  %s", cineStock().name);
  else if (s_noFilm) snprintf(status, sizeof(status), "NO FILM LOADED");
  else if (s_rollDone) snprintf(status, sizeof(status), "%s  " G_DOT "  %s", rollStock().name, isPack() ? "PACK EMPTY" : "ROLL FINISHED");
  else if (isPack()) snprintf(status, sizeof(status), "%s  " G_DOT "  %d LEFT", rollStock().name, s_rollLen - s_frame + 1);
  else snprintf(status, sizeof(status), "%s  " G_DOT "  FRAME %02d/%d", rollStock().name, s_frame, s_rollLen);
  bool chimed = false;
  for (;;) {
    const float t = (millis() - bootT0) / 1000.f;
    if (t > 2.9f) break;
    if (!chimed && t > .25f) { chimed = true; sound::play(sound::Fx::Chime); }
    ui::drawBoot(cv, t, status);
    present();
  }
  go(Scene::View);
  s_irisT0 = millis();
  s_fpsT0 = millis();
}

void loop() {
  const uint32_t now = millis();
  const float t = (now - s_sceneT0) / 1000.f;
  if (s_cine) flash::set(s_videoLight && (s_scene == Scene::View || s_scene == Scene::Menu));
  else if (s_shot != SHOT_EXPOSING) flash::set(false);  // while exposing, the capture task owns it
  clock_::tick();
  readSensors(now);
  updateLeds(now);
  const BtnEvent e = buttons::poll();

  switch (s_scene) {
    case Scene::View:
      if (e != BtnEvent::None) handleView(e);
      if (s_scene != Scene::View) break;  // a button just started something else
      if (!s_cine && !s_windT0 && s_needWind && s_easyWind) startWind(true);
      viewScene(now);
      break;

    case Scene::Timer: {
      const int rem = s_timer * 1000 - (int)(now - s_sceneT0);
      if (e == BtnEvent::B || e == BtnEvent::B_Long || e == BtnEvent::Knob || e == BtnEvent::KnobHold) {
        sound::play(sound::Fx::Tick);
        toast("TIMER CANCELLED");
        go(Scene::View);
        break;
      }
      if (rem <= 0) { shoot(); break; }
      if (now >= s_nextBeep) {
        sound::play(sound::Fx::Beep);
        s_nextBeep = now + (rem < 2000 ? 250 : 1000);  // faster for the last two seconds
      }
      if (renderLive()) {
        drawPhotoOverlay(now);
        ui::drawTimer(cv, rem);
      } else noCamera();
      present();
      break;
    }

    case Scene::Exposing:
      // The shutter is open: black, like looking at the back of the curtain.
      cv.fill(col::BLACK);
      cv.text(W / 2, 116, "HOLD STILL", rgb(0x3A, 0x36, 0x32), 1, Align::Center, false);
      present();
      if (s_shot == SHOT_DEVELOPED || s_shot == SHOT_DONE) {
        if (s_shotInstant) sound::play(sound::Fx::Motor);  // the print ejects
        go(s_shotInstant ? Scene::InstantDev : Scene::Darkroom);
      } else if (s_shot == SHOT_FAILED) {
        toast(s_shotMsg, 2500);
        go(Scene::View);
      }
      delay(20);
      break;

    case Scene::Darkroom: {
      if (s_shot == SHOT_FAILED) { toast(s_shotMsg, 2500); go(Scene::View); break; }
      const bool done = s_shot == SHOT_DONE;
      float p = (now - s_sceneT0) / (float)DEVELOP_MS;
      if (!done && p > .93f) p = .93f;  // wait for the save before the print is finished
      ui::drawDarkroom(cv, s_print, p, t);
      present();
      if (done && now - s_sceneT0 >= DEVELOP_MS) {
        if (s_shotMsg[0]) toast(s_shotMsg, 2500);
        go(Scene::Review);
      }
      break;
    }

    case Scene::InstantDev: {
      if (s_shot == SHOT_FAILED) { toast(s_shotMsg, 2500); go(Scene::View); break; }
      const bool done = s_shot == SHOT_DONE;
      float tt = t;
      if (!done && tt > EJECT_S + INSTANT_DEVELOP_S * .9f) tt = EJECT_S + INSTANT_DEVELOP_S * .9f;
      ui::drawInstantDevelop(cv, s_print, tt, EJECT_S, INSTANT_DEVELOP_S);
      present();
      if (done && t >= EJECT_S + INSTANT_DEVELOP_S) {
        if (s_shotMsg[0]) toast(s_shotMsg, 2500);
        go(Scene::Review);
      }
      break;
    }

    case Scene::Review:
      if (s_shotInstant) {
        char cap[32], d[16];
        time_t tn = time(nullptr);
        struct tm tm;
        localtime_r(&tn, &tm);
        static const char* MON[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
        snprintf(d, sizeof(d), "%s %d", MON[tm.tm_mon % 12], tm.tm_mday);
        snprintf(cap, sizeof(cap), "%s  " G_DOT "  #%02d", d, s_shotFrame);
        cv.fill(rgb(0x14, 0x12, 0x10));
        ui::drawInstantPrint(cv, s_print, 22, 0, cap);
        cv.text(W / 2, 226, "PRESS " G_RIGHT " GO", col::DIM, 1, Align::Center, false);
      } else {
        ui::drawReview(cv, s_print, t, s_shotFrame, rollStock());
      }
      drawToast(now);
      present();
      if (e == BtnEvent::A || e == BtnEvent::B || e == BtnEvent::Knob || now - s_sceneT0 >= REVIEW_MS) {
        s_shot = SHOT_NONE;
        go(Scene::View);
      }
      break;

    case Scene::Rewind:
      ui::drawRewind(cv, (now - s_sceneT0) / (float)REWIND_MS, s_frame, t);
      present();
      if (now - s_sceneT0 >= REWIND_MS) {
        unload();
        toast("ROLL SAVED");
        openShelf(true, Scene::View);
      }
      break;

    case Scene::Loading:
      if (!(s_loadSounds & 1) && t > 1.0f) { s_loadSounds |= 1; sound::play(sound::Fx::Wind); }
      if (!(s_loadSounds & 2) && t > 1.65f) { s_loadSounds |= 2; sound::play(sound::Fx::Wind); }
      ui::drawLoading(cv, t, film::STOCKS[s_loadStock]);
      present();
      if (now - s_sceneT0 >= LOADING_MS) finishLoading();
      break;

    case Scene::ModeSwitch:
      if (!s_modeSwitched && t > .35f) {
        // The iris is closed: change over the camera and the look.
        s_modeSwitched = true;
        s_cine = !s_cine;
        startView();
        film::select(s_cine ? cineStock() : rollStock());
        saveSettings();
      }
      if (s_cine && s_modeSwitched ? renderCine(now) : renderLive()) {
      } else {
        cv.fill(col::BLACK);
      }
      {
        const bool toCine = s_modeSwitched ? s_cine : !s_cine;
        ui::drawModeSwitch(cv, t, toCine ? "CINE" : "PHOTO",
                           toCine ? cineStock().name : (s_noFilm ? "NO FILM" : rollStock().name));
      }
      present();
      if (now - s_sceneT0 >= MODE_SWITCH_MS) go(Scene::View);
      break;

    case Scene::Menu: {
      const MenuItem* items;
      const int n = menuItems(&items);
      if (s_menuSel >= n) s_menuSel = 0;
      int step = tiltStep(now);
      if (e == BtnEvent::A || e == BtnEvent::TurnCW) step = 1;
      if (e == BtnEvent::TurnCCW) step = -1;
      if (step) { s_menuSel = (s_menuSel + step + n) % n; sound::play(sound::Fx::Tick); }
      if (e == BtnEvent::B || e == BtnEvent::Knob) { sound::play(sound::Fx::Tick); menuChange(items[s_menuSel]); }
      if (e == BtnEvent::B_Long || e == BtnEvent::KnobHold) { go(Scene::View); break; }
      if (s_scene != Scene::Menu) break;
      const bool live = s_cine ? renderCine(now) : renderLive();
      if (!live) noCamera();
      ui::MenuRow rows[20];
      for (int i = 0; i < n; i++) menuRow(items[i], rows[i]);
      ui::drawMenu(cv, rows, n, s_menuSel);
      drawToast(now);
      present();
      break;
    }

    case Scene::Shelf: {
      int step = tiltStep(now);
      if (e == BtnEvent::A || e == BtnEvent::TurnCW) step = 1;
      if (e == BtnEvent::TurnCCW) step = -1;
      if (step) {
        s_shelfSel = (s_shelfSel + step + film::STOCK_COUNT) % film::STOCK_COUNT;
        film::select(film::STOCKS[s_shelfSel]);
        sound::play(sound::Fx::Tick);
      }
      if (e == BtnEvent::B || e == BtnEvent::Knob) { startLoading(s_shelfSel); break; }
      if (e == BtnEvent::B_Long || e == BtnEvent::KnobHold) { closeShelf(); break; }
      s_shelfOff += (s_shelfSel - s_shelfOff) * .25f;
      if (renderLive()) scaleNearest(cv.pixels(), W, H, s_shelfPreview, 112, 84);
      ui::drawShelf(cv, s_shelfSel, s_shelfOff, s_noFilm ? -1 : s_stock, s_shelfForced, s_shelfPreview,
                    film::STOCKS[s_shelfSel].pack ? film::STOCKS[s_shelfSel].pack : s_nextRollLen);
      drawToast(now);
      present();
      break;
    }

    case Scene::Album: {
      int step = tiltStep(now);
      if (e == BtnEvent::A || e == BtnEvent::TurnCW) step = 1;
      if (e == BtnEvent::TurnCCW) step = -1;
      if (s_itemCount && step) { s_albumSel = (s_albumSel + step + s_itemCount) % s_itemCount; sound::play(sound::Fx::Tick); }
      if (e == BtnEvent::A_Long || e == BtnEvent::Knob) { openItem(); break; }
      if ((e == BtnEvent::AB || e == BtnEvent::PushTurnCW || e == BtnEvent::PushTurnCCW) && s_itemCount) {
        s_confirmBack = Scene::Album;
        s_confirmDelete = false;
        go(Scene::Confirm);
        break;
      }
      if (e == BtnEvent::B || e == BtnEvent::B_Long || e == BtnEvent::KnobHold) { go(s_albumBack); break; }
      s_albumOff += (s_albumSel - s_albumOff) * .25f;
      ui::drawAlbum(cv, s_items, s_itemCount, s_albumSel, s_albumOff, thumbFor);
      drawToast(now);
      present();
      break;
    }

    case Scene::Photo: {
      int step = 0;
      if (e == BtnEvent::A || e == BtnEvent::TurnCW) step = 1;
      if (e == BtnEvent::TurnCCW) step = -1;
      if (step && s_itemCount) {
        s_albumSel = (s_albumSel + step + s_itemCount) % s_itemCount;
        s_albumOff = (float)s_albumSel;
        sound::play(sound::Fx::Tick);
        if (s_items[s_albumSel].kind == gallery::CLIP) { go(Scene::Album); break; }
        openItem();
      }
      if (e == BtnEvent::AB || e == BtnEvent::Knob) {
        s_confirmBack = Scene::Photo;
        s_confirmDelete = false;
        go(Scene::Confirm);
        break;
      }
      if (e == BtnEvent::B || e == BtnEvent::B_Long || e == BtnEvent::KnobHold) { go(Scene::Album); break; }
      ui::drawPhotoView(cv, s_print, s_items[s_albumSel], s_albumSel, s_itemCount);
      drawToast(now);
      present();
      break;
    }

    case Scene::Play: {
      if (e == BtnEvent::B || e == BtnEvent::B_Long || e == BtnEvent::A || e == BtnEvent::Knob || e == BtnEvent::KnobHold) {
        player::close();
        go(Scene::Album);
        break;
      }
      bool changed;
      const bool more = player::update(s_print, &changed);
      memcpy(cv.pixels(), s_print, W * H * 2);
      ui::drawPlayback(cv, player::progress());
      present();
      if (!more) { player::close(); go(Scene::Album); }
      break;
    }

    case Scene::Confirm: {
      if (e == BtnEvent::TurnCW || e == BtnEvent::TurnCCW) { s_confirmDelete = !s_confirmDelete; sound::play(sound::Fx::Tick); }
      if (e == BtnEvent::A || (e == BtnEvent::Knob && s_confirmDelete)) { deleteSelected(); break; }
      if (e == BtnEvent::B || e == BtnEvent::B_Long || e == BtnEvent::KnobHold || (e == BtnEvent::Knob && !s_confirmDelete)) {
        go(s_confirmBack);
        break;
      }
      const gallery::Item& it = s_items[s_albumSel];
      if (s_confirmBack == Scene::Photo) ui::drawPhotoView(cv, s_print, it, s_albumSel, s_itemCount);
      else ui::drawAlbum(cv, s_items, s_itemCount, s_albumSel, s_albumOff, thumbFor);
      char l2[40];
      if (it.kind == gallery::CLIP) snprintf(l2, sizeof(l2), "CLIP AFT_%04u.AVI", it.num);
      else snprintf(l2, sizeof(l2), "PHOTO AFT_%04u.JPG", it.num);
      ui::drawConfirm(cv, it.kind == gallery::CLIP ? "DELETE CLIP?" : "DELETE PHOTO?", l2, s_confirmDelete);
      present();
      break;
    }

    case Scene::RewindCrank: {
      // Cranking by hand: one frame per click, back to S.
      if (e == BtnEvent::TurnCCW) { s_crankClicks++; sound::play(sound::Fx::Crank); }
      if (e == BtnEvent::B) { sound::play(sound::Fx::MotorLong); go(Scene::Rewind); break; }  // let the motor finish it
      if (e == BtnEvent::KnobHold || e == BtnEvent::B_Long) { go(Scene::View); break; }
      const float p = s_rollLen ? (float)s_crankClicks / s_rollLen : 1;
      ui::drawRewind(cv, p, s_frame, s_crankClicks * .1f);
      cv.text(W / 2, 214, "KEEP TURNING " G_LEFT, col::DIM, 1, Align::Center, false);
      present();
      if (p >= 1) {
        unload();
        sound::play(sound::Fx::Clunk);
        toast("ROLL SAVED");
        openShelf(true, Scene::View);
      }
      break;
    }

    case Scene::Transfer: {
      transfer::loop();
      if (e == BtnEvent::KnobHold || e == BtnEvent::B_Long || e == BtnEvent::A_Long) { exitTransfer(); break; }
      if (e == BtnEvent::Knob || e == BtnEvent::A || e == BtnEvent::B) {
        s_qrShowJoin = !s_qrShowJoin;
        s_qrFlipT = now;
        sound::play(sound::Fx::Tick);
      }
      if (now - s_qrFlipT > 8000) { s_qrShowJoin = !s_qrShowJoin; s_qrFlipT = now; }
      // Redraw a few times a second; the rest of the time goes to serving files.
      if (now - s_transferDrawT > 250) {
        s_transferDrawT = now;
        ui::drawTransfer(cv, transfer::status(), s_qrShowJoin ? s_qrJoin : s_qrOpen, s_qrShowJoin, now);
        drawToast(now);
        present();
      }
      break;
    }

    case Scene::DateSet:
      if (e == BtnEvent::A) { s_dateField = (s_dateField + 1) % 5; sound::play(sound::Fx::Tick); }
      if (e == BtnEvent::Knob) { s_dateField = (s_dateField + 1) % 5; sound::play(sound::Fx::Tick); }
      if (e == BtnEvent::B || e == BtnEvent::TurnCW) { dateStep(1); sound::play(sound::Fx::Tick); }
      if (e == BtnEvent::TurnCCW) { dateStep(-1); sound::play(sound::Fx::Tick); }
      if (e == BtnEvent::B_Long || e == BtnEvent::KnobHold) {
        s_dateEdit.tm_sec = 0;
        s_dateEdit.tm_isdst = 0;
        clock_::set(mktime(&s_dateEdit));
        sound::play(sound::Fx::Clunk);
        toast("DATE SET");
        go(Scene::Menu);
        break;
      }
      ui::drawDateSet(cv, s_dateEdit, s_dateField, now);
      present();
      break;
  }
}

}  // namespace cameraApp
