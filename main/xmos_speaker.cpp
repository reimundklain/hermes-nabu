#include "xmos_speaker.hpp"

#include <algorithm>
#include <cstdlib>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "speaker_queue.hpp"

namespace nabu {
namespace {

const char* TAG = "nabu.speaker";

constexpr gpio_num_t kBclkPin = GPIO_NUM_8;
constexpr gpio_num_t kWsPin = GPIO_NUM_7;
constexpr gpio_num_t kDataPin = GPIO_NUM_10;
constexpr gpio_num_t kAmpEnablePin = GPIO_NUM_47;

// About 1.5 s of mono PCM16; the server keeps 0.5 s ahead.
constexpr size_t kBufferBytes = XmosSpeaker::kRate * 3 / 2 * sizeof(int16_t);

constexpr uint32_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 7;
constexpr TickType_t kReceiveWait = pdMS_TO_TICKS(20);
// i2s_channel_write() takes milliseconds, not ticks.
constexpr uint32_t kWriteTimeoutMs = 200;

// PCM16 into the top half of a 32-bit slot.
constexpr int32_t kSampleScale = 1 << 16;
constexpr uint32_t kMsPerSecond = 1000;

}  // namespace

bool XmosSpeaker::begin(Aic3204& dac) {
  dac_ = &dac;
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
  chan.auto_clear = true;  // silence on underrun instead of repeating the last buffer
  esp_err_t err = i2s_new_channel(&chan, &tx_, nullptr);
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
  cfg.gpio_cfg.dout = kDataPin;
  cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  if ((err = i2s_channel_init_std_mode(tx_, &cfg)) != ESP_OK || (err = i2s_channel_enable(tx_)) != ESP_OK) {
    ESP_LOGE(TAG, "I2S start failed: %s", esp_err_to_name(err));
    return false;
  }

  auto* storage = static_cast<uint8_t*>(heap_caps_malloc(kBufferBytes + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  static StaticStreamBuffer_t control;
  if (storage) buffer_ = xStreamBufferCreateStatic(kBufferBytes, 1, storage, &control);
  if (!buffer_) {
    ESP_LOGE(TAG, "no memory for the %u byte playback buffer", static_cast<unsigned>(kBufferBytes));
    return false;
  }
  if (xTaskCreate(&XmosSpeaker::task, "nabu-spk", kTaskStack, this, kTaskPriority, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "can't start the playback task");
    return false;
  }

  gpio_config_t amp = {};
  amp.pin_bit_mask = 1ULL << kAmpEnablePin;
  amp.mode = GPIO_MODE_OUTPUT;
  if ((err = gpio_config(&amp)) != ESP_OK || (err = gpio_set_level(kAmpEnablePin, 1)) != ESP_OK) {
    ESP_LOGE(TAG, "amplifier enable on GPIO %d failed: %s", kAmpEnablePin, esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "speaker ready");
  return true;
}

bool XmosSpeaker::begin(uint32_t sample_rate) {
  if (!buffer_) return false;
  // The XMOS sets the clock; the server resamples to the rate in `hello`.
  if (sample_rate != kRate) {
    ESP_LOGE(TAG, "stream at %u Hz, the XMOS plays %u Hz", static_cast<unsigned>(sample_rate),
             static_cast<unsigned>(kRate));
    return false;
  }
  abort();
  played_frames_ = 0;
  failed_writes_ = 0;
  peak_ = 0;
  open_ = true;
  draining_ = false;
  ESP_LOGI(TAG, "stream at %u Hz", static_cast<unsigned>(sample_rate));
  if (dac_) dac_->log_status();
  return true;
}

void XmosSpeaker::write(const int16_t* samples, size_t count) {
  if (!open_) return;
  const size_t bytes = count * sizeof(int16_t);
  // The app task is the only writer, so the room can only grow before the send.
  const size_t fit = hg::whole_sample_bytes(bytes, xStreamBufferSpacesAvailable(buffer_));
  const size_t sent = xStreamBufferSend(buffer_, samples, fit, 0);
  if (sent < bytes) ESP_LOGW(TAG, "playback buffer full, dropped %u bytes", static_cast<unsigned>(bytes - sent));
}

void XmosSpeaker::end() {
  open_ = false;
  draining_ = true;
}

void XmosSpeaker::abort() {
  open_ = false;
  draining_ = false;
  flush_ = true;
}

bool XmosSpeaker::busy() const {
  return open_ || draining_ || (buffer_ && xStreamBufferBytesAvailable(buffer_) > 0);
}

void XmosSpeaker::set_volume(uint8_t percent) {
  if (dac_ && dac_->set_volume(percent) != ESP_OK) ESP_LOGW(TAG, "volume %u%% not applied", percent);
}

void XmosSpeaker::task(void* arg) {
  auto* self = static_cast<XmosSpeaker*>(arg);
  for (;;) self->play_chunk();
}

void XmosSpeaker::play_chunk() {
  if (flush_.exchange(false)) xStreamBufferReset(buffer_);
  const size_t got = xStreamBufferReceive(buffer_, mono_, sizeof(mono_), kReceiveWait);
  if (got == 0) {
    if (draining_ && !open_) {
      draining_ = false;  // everything queued has gone out
      ESP_LOGI(TAG, "played %u ms, peak %d, failed writes %u",
               static_cast<unsigned>(played_frames_ * kMsPerSecond / kRate), static_cast<int>(peak_),
               static_cast<unsigned>(failed_writes_));
    }
    return;
  }
  const size_t frames = got / sizeof(int16_t);
  int32_t peak = peak_;
  for (size_t i = 0; i < frames; ++i) {
    peak = std::max(peak, std::abs(static_cast<int32_t>(mono_[i])));
    const int32_t sample = static_cast<int32_t>(mono_[i]) * kSampleScale;
    stereo_[i * kSlots] = sample;
    stereo_[i * kSlots + 1] = sample;
  }
  size_t written = 0;
  const esp_err_t err = i2s_channel_write(tx_, stereo_, frames * kSlots * sizeof(int32_t), &written, kWriteTimeoutMs);
  peak_ = peak;
  played_frames_ += written / (kSlots * sizeof(int32_t));
  if (err != ESP_OK) ++failed_writes_;
}

}  // namespace nabu
