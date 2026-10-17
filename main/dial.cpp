#include "dial.hpp"

#include <cstdint>
#include <limits>

#include "driver/gpio.h"
#include "esp_log.h"

namespace nabu {
namespace {

const char* TAG = "nabu.dial";

constexpr gpio_num_t kPinA = GPIO_NUM_16;
constexpr gpio_num_t kPinB = GPIO_NUM_18;

// Both channels count both edges: four counts per quadrature cycle. The stock
// firmware reads two steps per cycle, one per detent.
constexpr int kCountsPerDetent = 2;

// The hardware counter is 16 bits; with accumulation on, the count read back
// runs on past these limits.
constexpr int kCountLimit = std::numeric_limits<int16_t>::max();

// Pulses shorter than this are noise, not contact bounce.
constexpr uint32_t kMaxGlitchNs = 1000;

// Counts the edges of `edge_pin`; the other pin being high reverses the
// direction.
esp_err_t add_channel(pcnt_unit_handle_t unit, gpio_num_t edge_pin, gpio_num_t level_pin,
                      pcnt_channel_edge_action_t on_rising, pcnt_channel_edge_action_t on_falling) {
  pcnt_chan_config_t config = {};
  config.edge_gpio_num = edge_pin;
  config.level_gpio_num = level_pin;
  pcnt_channel_handle_t channel = nullptr;
  esp_err_t err = pcnt_new_channel(unit, &config, &channel);
  if (err == ESP_OK) err = pcnt_channel_set_edge_action(channel, on_rising, on_falling);
  if (err == ESP_OK)
    err = pcnt_channel_set_level_action(channel, PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP);
  return err;
}

}  // namespace

bool Dial::begin() {
  pcnt_unit_config_t unit_config = {};
  unit_config.low_limit = -kCountLimit;
  unit_config.high_limit = kCountLimit;
  unit_config.flags.accum_count = true;
  pcnt_unit_handle_t unit = nullptr;
  esp_err_t err = pcnt_new_unit(&unit_config, &unit);

  pcnt_glitch_filter_config_t filter = {};
  filter.max_glitch_ns = kMaxGlitchNs;
  if (err == ESP_OK) err = pcnt_unit_set_glitch_filter(unit, &filter);

  // Clockwise, A leads B, as the stock firmware's decoder reads the dial: A
  // rises while B is low, and B falls while A is low.
  if (err == ESP_OK)
    err = add_channel(unit, kPinA, kPinB, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  if (err == ESP_OK)
    err = add_channel(unit, kPinB, kPinA, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);

  // Accumulation happens when the counter reaches a watch point at a limit.
  if (err == ESP_OK) err = pcnt_unit_add_watch_point(unit, -kCountLimit);
  if (err == ESP_OK) err = pcnt_unit_add_watch_point(unit, kCountLimit);
  if (err == ESP_OK) err = pcnt_unit_enable(unit);
  if (err == ESP_OK) err = pcnt_unit_clear_count(unit);
  if (err == ESP_OK) err = pcnt_unit_start(unit);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "dial on GPIO %d/%d failed: %s", kPinA, kPinB, esp_err_to_name(err));
    return false;
  }
  unit_ = unit;
  ESP_LOGI(TAG, "ready");
  return true;
}

int Dial::poll() {
  if (!unit_) return 0;
  int count = 0;
  const esp_err_t err = pcnt_unit_get_count(unit_, &count);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "reading the count failed: %s", esp_err_to_name(err));
    return 0;
  }
  // Truncates toward zero, so a half turn back and forth reports nothing.
  const int detents = (count - reported_count_) / kCountsPerDetent;
  reported_count_ += detents * kCountsPerDetent;
  return detents;
}

}  // namespace nabu
