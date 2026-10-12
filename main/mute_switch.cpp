#include "mute_switch.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

namespace nabu {
namespace {

const char* TAG = "nabu.mute";

// The board pulls the line; the stock firmware reads it without pull-ups.
constexpr gpio_num_t kPin = GPIO_NUM_3;
constexpr int kMutedLevel = 1;
constexpr uint8_t kDebouncePolls = 3;  // 3 polls at ~10 ms

bool read_muted() { return gpio_get_level(kPin) == kMutedLevel; }

}  // namespace

bool MuteSwitch::begin() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kPin;
  io.mode = GPIO_MODE_INPUT;
  const esp_err_t err = gpio_config(&io);
  if (err != ESP_OK) {
    // Without a readable switch, keep the microphone off rather than guess.
    ESP_LOGE(TAG, "mute switch on GPIO %d failed: %s; staying muted", kPin, esp_err_to_name(err));
    muted_ = true;
    return false;
  }
  readable_ = true;
  muted_ = read_muted();
  ESP_LOGI(TAG, "switch %s", muted_ ? "muted" : "open");
  return true;
}

bool MuteSwitch::poll() {
  if (!readable_) return false;
  if (read_muted() == muted_) {
    stable_ = 0;
    return false;
  }
  if (++stable_ < kDebouncePolls) return false;
  stable_ = 0;
  muted_ = !muted_;
  return true;
}

}  // namespace nabu
