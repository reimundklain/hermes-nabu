#include "led_ring.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip_rmt.h"

namespace nabu {
namespace {

const char* TAG = "nabu.leds";

constexpr gpio_num_t kDataPin = GPIO_NUM_21;
constexpr gpio_num_t kPowerPin = GPIO_NUM_45;
// The rail needs a moment before the LEDs take data.
constexpr TickType_t kPowerSettle = pdMS_TO_TICKS(20);

// Timings and RMT memory as in the stock firmware's esp32_rmt_led_strip.
constexpr led_strip_timings_t kTimings = {
    .t0h_ns = 300,
    .t1h_ns = 650,
    .t0l_ns = 975,
    .t1l_ns = 650,
    .reset_us = 300,
};
constexpr size_t kRmtSymbols = 192;

// Logical position 0 is LED 7 of the chain, as in the stock firmware's
// partition of the ring, so clockwise and the start match it.
constexpr size_t kFirstLed = 7;

// The stock firmware's default ring brightness (66 %).
constexpr uint16_t kBrightness = 168;
constexpr uint16_t kFull = 255;

uint8_t dimmed(uint8_t value) { return static_cast<uint8_t>(value * kBrightness / kFull); }

}  // namespace

bool LedRing::begin() {
  gpio_config_t power = {};
  power.pin_bit_mask = 1ULL << kPowerPin;
  power.mode = GPIO_MODE_OUTPUT;
  esp_err_t err = gpio_config(&power);
  if (err == ESP_OK) err = gpio_set_level(kPowerPin, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LED power on GPIO %d failed: %s", kPowerPin, esp_err_to_name(err));
    return false;
  }
  vTaskDelay(kPowerSettle);

  led_strip_config_t strip = {};
  strip.strip_gpio_num = kDataPin;
  strip.max_leds = kRingSize;
  strip.led_model = LED_MODEL_CUSTOM;
  strip.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  strip.timings = kTimings;
  led_strip_rmt_config_t rmt = {};
  rmt.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt.mem_block_symbols = kRmtSymbols;
  err = led_strip_new_rmt_device(&strip, &rmt, &strip_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LED strip on GPIO %d failed: %s", kDataPin, esp_err_to_name(err));
    strip_ = nullptr;
    return false;
  }
  ESP_LOGI(TAG, "%u LEDs ready", static_cast<unsigned>(kRingSize));
  return true;
}

void LedRing::show(const RingFrame& frame) {
  if (!strip_ || (shown_valid_ && frame == shown_)) return;
  for (size_t i = 0; i < kRingSize; ++i) {
    const Rgb& c = frame[i];
    led_strip_set_pixel(strip_, (i + kFirstLed) % kRingSize, dimmed(c.r), dimmed(c.g), dimmed(c.b));
  }
  const esp_err_t err = led_strip_refresh(strip_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "refresh failed: %s", esp_err_to_name(err));
    shown_valid_ = false;
    return;
  }
  shown_ = frame;
  shown_valid_ = true;
}

}  // namespace nabu
