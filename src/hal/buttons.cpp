#include "buttons.h"
#include "encoder.h"
#include "xl9535.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "../board/k10_pins.h"

static constexpr uint32_t LONG_MS = 600;
static constexpr uint32_t COMBO_MS = 650;
static constexpr int POLL_MS = 10;

struct Btn {
  bool down = false;
  bool raw = false;
  uint8_t stable = 0;   // consecutive samples agreeing with raw
  uint32_t t0 = 0;
  bool longFired = false;
  bool used = false;    // consumed by a combo or a push-turn, so its release does nothing
};

static QueueHandle_t s_q = nullptr;
static Btn s_a, s_b, s_k;
static bool s_comboArmed = false;
static uint32_t s_comboT0 = 0;
static volatile bool s_reversed = false;
static volatile int s_perClick = 4;
static int s_counts = 0;  // raw encoder counts not yet turned into clicks
// The knob switch is ignored until it has been seen released for a moment:
// unwired, or wired without its pull-up, it reads as held down forever.
static int s_knobReleasedFor = 0;
static bool s_knobTrusted = false;

static inline uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }
static void emit(BtnEvent e) { xQueueSend(s_q, &e, 0); }

// Debounce: a change must hold for two samples (20 ms). Returns +1 pressed, -1 released, 0 no change.
static int debounce(Btn& b, bool raw) {
  if (raw == b.raw) { if (b.stable < 255) b.stable++; } else { b.raw = raw; b.stable = 0; }
  if (b.stable == 2 && b.raw != b.down) return b.raw ? 1 : -1;
  return 0;
}

static void stepButtons(uint32_t now, bool rawA, bool rawB) {
  Btn* bs[2] = {&s_a, &s_b};
  const bool raws[2] = {rawA, rawB};
  for (int i = 0; i < 2; i++) {
    Btn& b = *bs[i];
    Btn& o = *bs[1 - i];
    const int d = debounce(b, raws[i]);
    if (d > 0) {
      b.down = true; b.t0 = now; b.longFired = false; b.used = false;
      if (o.down && !o.used && !o.longFired) { s_comboArmed = true; s_comboT0 = now; }
    } else if (d < 0) {
      b.down = false;
      if (b.used || b.longFired) continue;
      if (o.down) { s_comboArmed = false; o.used = true; continue; }  // both tapped: ignore
      emit(i == 0 ? BtnEvent::A : BtnEvent::B);
    }
  }
  if (s_comboArmed) {
    if (!(s_a.down && s_b.down)) s_comboArmed = false;
    else if (now - s_comboT0 >= COMBO_MS) {
      s_comboArmed = false;
      s_a.used = s_b.used = true;
      emit(BtnEvent::AB);
    }
  }
  for (int i = 0; i < 2; i++) {
    Btn& b = *bs[i];
    Btn& o = *bs[1 - i];
    if (b.down && !b.used && !b.longFired && !o.down && now - b.t0 >= LONG_MS) {
      b.longFired = true;
      emit(i == 0 ? BtnEvent::A_Long : BtnEvent::B_Long);
    }
  }
}

static void stepKnob(uint32_t now, bool rawSw) {
  if (!s_knobTrusted) {
    s_knobReleasedFor = rawSw ? 0 : s_knobReleasedFor + 1;
    if (s_knobReleasedFor >= 10) s_knobTrusted = true;  // 100 ms released
    rawSw = false;
  }
  const int d = debounce(s_k, rawSw);
  if (d > 0) {
    s_k.down = true; s_k.t0 = now; s_k.longFired = false; s_k.used = false;
  } else if (d < 0) {
    s_k.down = false;
    if (!s_k.used && !s_k.longFired) emit(BtnEvent::Knob);
  }

  s_counts += encoder::readDelta() * (s_reversed ? -1 : 1);
  const int per = s_perClick;
  while (s_counts >= per || s_counts <= -per) {
    const bool cw = s_counts > 0;
    s_counts += cw ? -per : per;
    if (s_k.down && !s_k.longFired) {
      s_k.used = true;  // a turn while held: not a press, not a hold
      emit(cw ? BtnEvent::PushTurnCW : BtnEvent::PushTurnCCW);
    } else if (!s_k.down) {
      emit(cw ? BtnEvent::TurnCW : BtnEvent::TurnCCW);
    }
  }

  if (s_k.down && !s_k.used && !s_k.longFired && now - s_k.t0 >= LONG_MS) {
    s_k.longFired = true;
    emit(BtnEvent::KnobHold);
  }
}

static void task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    uint16_t in;
    const uint32_t now = nowMs();
    if (xl9535::readInputs(&in)) {
      // All active low.
      stepButtons(now, !(in & (1u << K10_EXP_KEY_A)), !(in & (1u << K10_EXP_KEY_B)));
      stepKnob(now, !(in & (1u << K10_EXP_KNOB_SW)));
    } else {
      stepKnob(now, s_k.raw);  // keep counting clicks even if one expander read fails
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(POLL_MS));
  }
}

namespace buttons {

bool begin() {
  if (!xl9535::ok()) return false;
  encoder::begin();
  s_q = xQueueCreate(32, sizeof(BtnEvent));
  xTaskCreatePinnedToCore(task, "buttons", 3072, nullptr, 5, nullptr, 0);
  return true;
}

BtnEvent poll() {
  BtnEvent e = BtnEvent::None;
  if (s_q) xQueueReceive(s_q, &e, 0);
  return e;
}

bool downA() { return s_a.down; }
bool downB() { return s_b.down; }
bool downKnob() { return s_k.down; }

void setKnob(bool reversed, int countsPerClick) {
  s_reversed = reversed;
  s_perClick = countsPerClick > 0 ? countsPerClick : 4;
}

const char* name(BtnEvent e) {
  switch (e) {
    case BtnEvent::A: return "A";
    case BtnEvent::B: return "B";
    case BtnEvent::A_Long: return "HOLD A";
    case BtnEvent::B_Long: return "HOLD B";
    case BtnEvent::AB: return "A+B";
    case BtnEvent::Knob: return "KNOB PRESS";
    case BtnEvent::KnobHold: return "KNOB HOLD";
    case BtnEvent::TurnCW: return "TURN RIGHT";
    case BtnEvent::TurnCCW: return "TURN LEFT";
    case BtnEvent::PushTurnCW: return "PUSH+TURN RIGHT";
    case BtnEvent::PushTurnCCW: return "PUSH+TURN LEFT";
    default: return "";
  }
}

}  // namespace buttons
