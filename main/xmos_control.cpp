#include "xmos_control.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace nabu {
namespace {

const char* TAG = "nabu.xmos";

constexpr gpio_num_t kResetPin = GPIO_NUM_4;
constexpr int kResetAsserted = 1;  // active high
constexpr int kResetReleased = 0;
constexpr uint32_t kResetPulseUs = 1000;
constexpr int64_t kBootTimeUs = 3'000'000;
constexpr int64_t kUsPerMs = 1000;

constexpr uint16_t kAddress = 0x42;
constexpr uint32_t kBusSpeedHz = 400'000;
constexpr int kTimeoutMs = 100;

// Configuration service of the ffva firmware.
constexpr uint8_t kConfigurationService = 241;
constexpr uint8_t kReadBit = 0x80;
constexpr uint8_t kChannel0Stage = 0x30;
constexpr uint8_t kChannel1Stage = 0x40;

// How far along the pipeline a channel's audio is taken.
enum Stage : uint8_t { None = 0, EchoCancelled = 1, InterferenceCancelled = 2, NoiseSuppressed = 3, GainControlled = 4 };

// As on the stock firmware: channel 0 carries speech for the assistant
// through the whole pipeline; channel 1 stops before gain control.
constexpr Stage kChannel0 = GainControlled;
constexpr Stage kChannel1 = NoiseSuppressed;

}  // namespace

esp_err_t XmosControl::reset() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kResetPin;
  io.mode = GPIO_MODE_OUTPUT;
  esp_err_t err = gpio_config(&io);
  if (err == ESP_OK) err = gpio_set_level(kResetPin, kResetAsserted);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "reset on GPIO %d failed: %s", kResetPin, esp_err_to_name(err));
    return err;
  }
  esp_rom_delay_us(kResetPulseUs);
  err = gpio_set_level(kResetPin, kResetReleased);
  reset_at_us_ = esp_timer_get_time();
  return err;
}

esp_err_t XmosControl::configure(i2c_master_bus_handle_t bus) {
  const int64_t booted_at_us = reset_at_us_ + kBootTimeUs;
  const int64_t wait_us = booted_at_us - esp_timer_get_time();
  if (wait_us > 0) vTaskDelay(pdMS_TO_TICKS(wait_us / kUsPerMs));

  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = kAddress;
  dev.scl_speed_hz = kBusSpeedHz;
  esp_err_t err = i2c_master_bus_add_device(bus, &dev, &device_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adding I2C device 0x%02x failed: %s", kAddress, esp_err_to_name(err));
    return err;
  }

  if ((err = write_stage(kChannel0Stage, kChannel0)) != ESP_OK) return err;
  if ((err = write_stage(kChannel1Stage, kChannel1)) != ESP_OK) return err;
  uint8_t stage = None;
  if ((err = read_stage(kChannel0Stage, stage)) != ESP_OK) return err;
  if (stage != kChannel0) {
    ESP_LOGE(TAG, "channel 0 stage reads back %u, expected %u", stage, kChannel0);
    return ESP_ERR_INVALID_RESPONSE;
  }
  ESP_LOGI(TAG, "ready: channel 0 stage %u, channel 1 stage %u", kChannel0, kChannel1);
  return ESP_OK;
}

esp_err_t XmosControl::write_stage(uint8_t channel_register, uint8_t stage) {
  constexpr uint8_t kPayloadLength = 1;
  const uint8_t request[] = {kConfigurationService, channel_register, kPayloadLength, stage};
  const esp_err_t err = i2c_master_transmit(device_, request, sizeof(request), kTimeoutMs);
  if (err != ESP_OK) ESP_LOGE(TAG, "writing stage register 0x%02x failed: %s", channel_register, esp_err_to_name(err));
  return err;
}

// The reply is a status byte followed by the value.
esp_err_t XmosControl::read_stage(uint8_t channel_register, uint8_t& stage) {
  constexpr uint8_t kReplyLength = 2;
  constexpr size_t kReplyValue = 1;
  const uint8_t request[] = {kConfigurationService, static_cast<uint8_t>(channel_register | kReadBit), kReplyLength};
  uint8_t reply[kReplyLength] = {};
  esp_err_t err = i2c_master_transmit(device_, request, sizeof(request), kTimeoutMs);
  if (err == ESP_OK) err = i2c_master_receive(device_, reply, sizeof(reply), kTimeoutMs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "reading stage register 0x%02x failed: %s", channel_register, esp_err_to_name(err));
    return err;
  }
  stage = reply[kReplyValue];
  return ESP_OK;
}

}  // namespace nabu
