#include "sound.h"
#include <math.h>
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "../hal/audio.h"

namespace {

constexpr float FS = audio::RATE;
constexpr int MAX_VOICES = 10;
constexpr int CHUNK = 256;

enum class Kind : uint8_t { Noise, Thunk, Tone, Tri, Motor, Sweep };

struct Voice {
  Kind kind;
  float t0, dur, f, gain, q;
  // state
  float phase, b0, b2, a1, a2, x1, x2, y1, y2, lp;
};

uint32_t s_rng = 0x1234567;
inline float noise() {
  s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
  return (int32_t)s_rng * (1.f / 2147483648.f);
}

// RBJ band-pass, constant 0 dB peak gain.
void bandpass(Voice& v, float f, float q) {
  if (f > FS * 0.45f) f = FS * 0.45f;
  const float w = 2 * (float)M_PI * f / FS, alpha = sinf(w) / (2 * q), a0 = 1 + alpha;
  v.b0 = alpha / a0; v.b2 = -alpha / a0; v.a1 = -2 * cosf(w) / a0; v.a2 = (1 - alpha) / a0;
}

struct Patch {
  Voice v[MAX_VOICES];
  int n = 0;
  float end = 0;
  Voice& add(Kind k, float t0, float dur, float f, float gain, float q = 1) {
    Voice& x = v[n++];
    x = {};
    x.kind = k; x.t0 = t0; x.dur = dur; x.f = f; x.gain = gain; x.q = q;
    if (k == Kind::Noise || k == Kind::Motor) bandpass(x, k == Kind::Motor ? 3000 : f, k == Kind::Motor ? .8f : q);
    if (t0 + dur > end) end = t0 + dur;
    return x;
  }
  void noiseHit(float t0, float dur, float f, float g, float q = 1) { add(Kind::Noise, t0, dur, f, g, q); }
  void thunk(float t0, float f, float dur, float g) { add(Kind::Thunk, t0, dur, f, g); }
  void tone(float t0, float f, float dur, float g) { add(Kind::Tone, t0, dur, f, g); }
  void tri(float t0, float f, float dur, float g) { add(Kind::Tri, t0, dur, f, g); }
  void motor(float t0, float dur) { add(Kind::Motor, t0, dur, 95, .12f); }
  // Sine gliding from f0 to f1 (q holds f1).
  void sweep(float t0, float f0, float f1, float dur, float g) { add(Kind::Sweep, t0, dur, f0, g, f1); }
};

// One sample of a voice at local time t (seconds since the voice started).
float sample(Voice& v, float t) {
  switch (v.kind) {
    case Kind::Noise: {
      // Web Audio exponentialRampToValueAtTime(.001, dur): e^(-6.9 t/dur)
      const float env = v.gain * expf(-6.9f * t / v.dur);
      const float x = noise();
      const float y = v.b0 * x + v.b2 * v.x2 - v.a1 * v.y1 - v.a2 * v.y2;
      v.x2 = v.x1; v.x1 = x; v.y2 = v.y1; v.y1 = y;
      return y * env;
    }
    case Kind::Thunk: {
      // Sine sweeping from 2f down to 0.6f, decaying.
      const float f = v.f * 2 * powf(0.3f, t / v.dur);
      v.phase += f / FS;
      return sinf(v.phase * 2 * (float)M_PI) * v.gain * expf(-6.9f * t / v.dur);
    }
    case Kind::Tone:
    case Kind::Tri: {
      v.phase += v.f / FS;
      v.phase -= floorf(v.phase);
      float s = v.kind == Kind::Tone ? (v.phase < .5f ? 1.f : -1.f) : 4 * fabsf(v.phase - .5f) - 1;
      float env = 1;
      if (t < .005f) env = t / .005f;
      else if (v.dur - t < .01f) env = (v.dur - t) / .01f;
      if (v.kind == Kind::Tri) env *= expf(-4.6f * t / v.dur);  // like the mockup's chime decay
      return s * v.gain * env;
    }
    case Kind::Sweep: {
      // Exponential glide up, like the oscillator in a flash charger, fading as it tops out.
      const float p = t / v.dur, f = v.f * powf(v.q / v.f, p);
      v.phase += f / FS;
      v.phase -= floorf(v.phase);
      float env = p < .05f ? p / .05f : 1 - .6f * p;
      if (v.dur - t < .08f) env *= (v.dur - t) / .08f;
      return sinf(v.phase * 2 * (float)M_PI) * v.gain * env;
    }
    case Kind::Motor: {
      // Sawtooth 95 -> 150 -> 120 Hz through a low-pass, plus gear noise.
      const float p = t / v.dur;
      const float f = p < .3f ? 95 + 55 * (p / .3f) : 150 - 30 * ((p - .3f) / .7f);
      v.phase += f / FS;
      v.phase -= floorf(v.phase);
      v.lp += (2 * v.phase - 1 - v.lp) * 0.38f;  // ~1.2 kHz one-pole
      const float x = noise();
      const float y = v.b0 * x + v.b2 * v.x2 - v.a1 * v.y1 - v.a2 * v.y2;
      v.x2 = v.x1; v.x1 = x; v.y2 = v.y1; v.y1 = y;
      float env = 1;
      if (t < .04f) env = t / .04f;
      else if (v.dur - t < .06f) env = (v.dur - t) / .06f;
      return (v.lp * .12f + y * .05f) * env * 2.2f;
    }
  }
  return 0;
}

void build(sound::Fx id, Patch& fx) {
  using sound::Fx;
  switch (id) {
    case Fx::Chime: fx.tri(0, 659, .5f, .12f); fx.tri(.12f, 988, .5f, .12f); fx.tri(.24f, 1319, .5f, .12f); break;
    case Fx::ShutterOpen: fx.noiseHit(0, .05f, 2500, .9f, .7f); fx.noiseHit(0, .03f, 6000, .4f); fx.thunk(0, 120, .08f, .5f); break;
    case Fx::ShutterClose: fx.noiseHit(0, .06f, 1800, .8f, .8f); fx.thunk(0, 90, .1f, .5f); break;
    case Fx::Wind:
      for (int i = 0; i < 7; i++) fx.noiseHit(i * .055f, .025f, 3400 + (esp_random() % 900), .35f, 2);
      fx.thunk(.4f, 160, .06f, .4f);
      break;
    case Fx::Thud: fx.thunk(0, 80, .09f, .6f); fx.noiseHit(0, .02f, 900, .3f); break;
    case Fx::Tick: fx.noiseHit(0, .012f, 4000, .25f, 3); break;
    case Fx::Clunk: fx.thunk(0, 70, .15f, .8f); fx.noiseHit(0, .04f, 1200, .5f); break;
    case Fx::Motor: fx.motor(0, .45f); break;
    case Fx::MotorLong: fx.motor(0, 2.2f); break;
    case Fx::Beep: fx.tone(0, 2400, .07f, .07f); break;
    case Fx::Ratchet: fx.noiseHit(0, .025f, 3400 + (esp_random() % 900), .4f, 2); break;
    case Fx::WindStop: fx.noiseHit(0, .02f, 2600, .35f, 1.5f); fx.thunk(0, 160, .06f, .45f); break;
    case Fx::FlashCharge: fx.sweep(0, 1400, 6800, 1.8f, .045f); break;
    case Fx::Crank: fx.noiseHit(0, .018f, 2000 + (esp_random() % 500), .3f, 1.5f); fx.thunk(0, 220, .03f, .15f); break;
  }
}

QueueHandle_t s_q = nullptr;
bool s_enabled = true;

void task(void*) {
  static Patch fx;
  int16_t buf[CHUNK];
  sound::Fx id;
  for (;;) {
    if (xQueueReceive(s_q, &id, portMAX_DELAY) != pdTRUE) continue;
    fx = Patch();
    build(id, fx);
    const int total = (int)(fx.end * FS) + CHUNK;
    for (int base = 0; base < total; base += CHUNK) {
      for (int i = 0; i < CHUNK; i++) {
        const float t = (base + i) / FS;
        float s = 0;
        for (int k = 0; k < fx.n; k++) {
          Voice& v = fx.v[k];
          if (t >= v.t0 && t < v.t0 + v.dur) s += sample(v, t - v.t0);
        }
        s *= .55f * 32767;  // same master level as the mockup
        buf[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
      }
      audio::play(buf, CHUNK);
    }
  }
}

}  // namespace

namespace sound {

void begin() {
  if (s_q) return;
  s_q = xQueueCreate(8, sizeof(Fx));
  xTaskCreatePinnedToCore(task, "sound", 4096, nullptr, 4, nullptr, 0);
}

void play(Fx fx) {
  if (s_q && s_enabled && audio::speakerOk()) xQueueSend(s_q, &fx, 0);
}

void setEnabled(bool on) { s_enabled = on; }

}  // namespace sound
