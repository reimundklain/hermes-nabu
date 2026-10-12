#include "xmos_mic.hpp"

#include <algorithm>
#include <cstdlib>

#include "esp_log.h"
#include "freertos/task.h"

namespace nabu {
namespace {

const char* TAG = "nabu.mic";

constexpr gpio_num_t kBclkPin = GPIO_NUM_13;
constexpr gpio_num_t kWsPin = GPIO_NUM_14;
constexpr gpio_num_t kDataPin = GPIO_NUM_15;

constexpr const char* kTaskName = "nabu-mic";
constexpr uint32_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 6;
constexpr TickType_t kIdlePoll = pdMS_TO_TICKS(10);
// i2s_channel_read() takes milliseconds, not ticks. Well above the 15 ms the
// XMOS takes to fill a DMA buffer.
constexpr uint32_t kReadTimeoutMs = 100;

// The XMOS output is already gain-controlled: keep the top 16 bits.
constexpr int kSampleShift = 16;
constexpr size_t kChannel = 0;
constexpr uint32_t kMsPerSecond = 1000;

}  // namespace

bool XmosMic::begin() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
  esp_err_t err = i2s_new_channel(&chan, nullptr, &rx_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2S channel failed: %s", esp_err_to_name(err));
    return false;
  }
  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate);
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.bclk = kBclkPin;
  cfg.gpio_cfg.ws = kWsPin;
  cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.din = kDataPin;
  err = i2s_channel_init_std_mode(rx_, &cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2S init failed: %s", esp_err_to_name(err));
    return false;
  }
  if (xTaskCreate(&XmosMic::task, kTaskName, kTaskStack, this, kTaskPriority, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "can't start the capture task");
    return false;
  }
  ESP_LOGI(TAG, "microphone ready");
  return true;
}

bool XmosMic::start(uint32_t sample_rate) {
  if (!rx_) return false;
  if (muted_) {
    ESP_LOGI(TAG, "muted by the switch; not capturing");
    return false;
  }
  // The XMOS sets the clock; it can't follow another rate.
  if (sample_rate != kRate) {
    ESP_LOGE(TAG, "asked for %u Hz, the XMOS sends %u Hz", static_cast<unsigned>(sample_rate),
             static_cast<unsigned>(kRate));
    return false;
  }
  const esp_err_t err = i2s_channel_enable(rx_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "starting capture failed: %s", esp_err_to_name(err));
    return false;
  }
  captured_frames_ = 0;
  failed_reads_ = 0;
  peak_ = 0;
  capturing_ = true;
  return true;
}

void XmosMic::stop() {
  if (!capturing_) return;
  capturing_ = false;
  i2s_channel_disable(rx_);
  ESP_LOGI(TAG, "captured %u ms, peak %d, failed reads %u",
           static_cast<unsigned>(captured_frames_ * kMsPerSecond / kRate), static_cast<int>(peak_),
           static_cast<unsigned>(failed_reads_));
}

void XmosMic::task(void* arg) {
  auto* self = static_cast<XmosMic*>(arg);
  for (;;) {
    if (!self->capturing_) {
      vTaskDelay(kIdlePoll);
      continue;
    }
    self->capture_chunk();
  }
}

void XmosMic::capture_chunk() {
  size_t got = 0;
  // A read that times out still returns what it got; keep that part.
  const esp_err_t err = i2s_channel_read(rx_, raw_, sizeof(raw_), &got, kReadTimeoutMs);
  if (err != ESP_OK) ++failed_reads_;
  if (got == 0) return;
  const size_t frames = got / (sizeof(int32_t) * kSlots);
  int32_t peak = peak_;
  for (size_t i = 0; i < frames; ++i) {
    pcm_[i] = static_cast<int16_t>(raw_[i * kSlots + kChannel] >> kSampleShift);
    peak = std::max(peak, std::abs(static_cast<int32_t>(pcm_[i])));
  }
  peak_ = peak;
  captured_frames_ += frames;
  if (capturing_ && !muted_) hgp::events::post(hgp::EventType::Mic, pcm_, frames * sizeof(int16_t));
}

}  // namespace nabu
