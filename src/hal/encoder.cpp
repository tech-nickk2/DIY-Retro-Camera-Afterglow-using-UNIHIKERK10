#include "encoder.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "../board/k10_pins.h"

static const char* TAG = "encoder";
static pcnt_unit_handle_t s_unit = nullptr;
static int s_last = 0, s_total = 0;
static constexpr int LIMIT = 10000;

namespace encoder {

bool begin() {
  pcnt_unit_config_t uc = {};
  uc.low_limit = -LIMIT;
  uc.high_limit = LIMIT;
  if (pcnt_new_unit(&uc, &s_unit) != ESP_OK) { ESP_LOGE(TAG, "no PCNT unit"); return false; }
  pcnt_glitch_filter_config_t filter = {};
  filter.max_glitch_ns = 1000;  // ignore contact bounce shorter than 1 us
  pcnt_unit_set_glitch_filter(s_unit, &filter);

  // Full quadrature: each channel counts the edges of one pin, in the
  // direction given by the level of the other. The driver enables pull-ups.
  pcnt_chan_config_t ca = {};
  ca.edge_gpio_num = K10_KNOB_A;
  ca.level_gpio_num = K10_KNOB_B;
  pcnt_channel_handle_t chA, chB;
  pcnt_new_channel(s_unit, &ca, &chA);
  pcnt_chan_config_t cb = {};
  cb.edge_gpio_num = K10_KNOB_B;
  cb.level_gpio_num = K10_KNOB_A;
  pcnt_new_channel(s_unit, &cb, &chB);
  pcnt_channel_set_edge_action(chA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(chA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  pcnt_channel_set_edge_action(chB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(chB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  pcnt_unit_enable(s_unit);
  pcnt_unit_clear_count(s_unit);
  pcnt_unit_start(s_unit);
  return true;
}

int readDelta() {
  if (!s_unit) return 0;
  int now = 0;
  pcnt_unit_get_count(s_unit, &now);
  int d = now - s_last;
  s_last = now;
  if (now > LIMIT / 2 || now < -LIMIT / 2) {  // stay well away from the counter's limits
    pcnt_unit_clear_count(s_unit);
    s_last = 0;
  }
  s_total += d;
  return d;
}

int total() { return s_total; }

}  // namespace encoder
