#include "aic3204.hpp"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace nabu {
namespace {

const char* TAG = "nabu.dac";

constexpr uint16_t kAddress = 0x18;
constexpr uint32_t kBusSpeedHz = 400'000;
constexpr int kTimeoutMs = 100;

constexpr uint32_t kResetSettleUs = 1000;
// Headphone soft stepping (see kHeadphoneStartup) needs this before the DACs
// power on, or the output pops.
constexpr int64_t kSoftStartUs = 2'500'000;

constexpr int64_t kUsPerMs = 1000;

// Page 0
constexpr uint8_t kPageSelect = 0x00;
constexpr uint8_t kSoftwareReset = 0x01;
constexpr uint8_t kNdac = 0x0B;
constexpr uint8_t kMdac = 0x0C;
constexpr uint8_t kDosr = 0x0E;
constexpr uint8_t kInterface1 = 0x1B;
constexpr uint8_t kInterface4 = 0x1F;
constexpr uint8_t kInterface5 = 0x20;
constexpr uint8_t kSclkMfp3 = 0x38;
constexpr uint8_t kDacProcessingBlock = 0x3C;
constexpr uint8_t kDacSetup1 = 0x3F;
constexpr uint8_t kDacSetup2 = 0x40;
constexpr uint8_t kDacLeftVolume = 0x41;
constexpr uint8_t kDacRightVolume = 0x42;
constexpr uint8_t kDacFlags1 = 0x25;  // DAC and output driver power status
constexpr uint8_t kDacFlags2 = 0x26;  // DAC PGA gain status

// Page 1
constexpr uint8_t kPowerConfig = 0x01;
constexpr uint8_t kLdoControl = 0x02;
constexpr uint8_t kPlaybackConfig1 = 0x03;
constexpr uint8_t kPlaybackConfig2 = 0x04;
constexpr uint8_t kOutputPower = 0x09;
constexpr uint8_t kCommonMode = 0x0A;
constexpr uint8_t kHplRoute = 0x0C;
constexpr uint8_t kHprRoute = 0x0D;
constexpr uint8_t kLolRoute = 0x0E;
constexpr uint8_t kLorRoute = 0x0F;
constexpr uint8_t kHplGain = 0x10;
constexpr uint8_t kHprGain = 0x11;
constexpr uint8_t kLolGain = 0x12;
constexpr uint8_t kLorGain = 0x13;
constexpr uint8_t kHeadphoneStartup = 0x14;
constexpr uint8_t kReferenceStartup = 0x7B;

struct RegisterWrite {
  uint8_t reg;
  uint8_t value;
};

// Clocks: MCLK 24.576 MHz from the XMOS = NDAC 2 * MDAC 2 * DOSR 128 * 48 kHz.
constexpr RegisterWrite kPage0Setup[] = {
    {kNdac, 0x82},                // NDAC powered, divide by 2
    {kMdac, 0x82},                // MDAC powered, divide by 2
    {kDosr, 0x80},                // DAC oversampling 128
    {kInterface1, 0x30},          // I2S, 32-bit words
    {kSclkMfp3, 0x02},            // SCLK/MFP3 as audio data in
    {kInterface4, 0x01},          // secondary interface settings for that data input
    {kInterface5, 0x01},
    {kDacProcessingBlock, 0x01},  // processing block PRB_P1
};

constexpr RegisterWrite kPage1Setup[] = {
    {kLdoControl, 0x09},        // internal AVdd LDO on
    {kPowerConfig, 0x08},       // crude AVdd off now that the LDO supplies it
    {kLdoControl, 0x01},        // master analog power on
    {kCommonMode, 0x40},        // 0.75 V common mode: AVdd is only 1.72 V
    {kPlaybackConfig1, 0x00},   // left DAC PowerTune PTM_P3/4, class-AB driver
    {kPlaybackConfig2, 0x00},   // right DAC likewise
    {kReferenceStartup, 0x01},  // reference charges in 40 ms
    {kHeadphoneStartup, 0x25},  // soft stepping against power-up pop
    {kHplRoute, 0x08},          // left DAC to HPL
    {kHprRoute, 0x08},          // right DAC to HPR
    {kLolRoute, 0x08},          // left DAC to LOL
    {kLorRoute, 0x08},          // right DAC to LOR
    {kHplGain, 0x3E},           // HPL unmuted, -2 dB for the low common mode
    {kHprGain, 0x3E},           // HPR likewise
    {kLolGain, 0x00},           // LOL unmuted, 0 dB
    {kLorGain, 0x00},           // LOR likewise
    {kOutputPower, 0x3C},       // HPL, HPR, LOL, LOR drivers on
};

constexpr uint8_t kResetValue = 0x01;
constexpr uint8_t kDacChannelsOn = 0xD4;  // both on, left to left, right to right, soft step
constexpr uint8_t kDacUnmuted = 0x00;
constexpr uint8_t kDacMuted = 0x0C;

// Digital volume: signed, 0.5 dB per step, -63.5 dB to +24 dB.
constexpr int kVolumeMinStep = -127;
constexpr int kVolumeMaxStep = 48;
constexpr int kPercentMax = 100;

}  // namespace

esp_err_t Aic3204::begin(i2c_master_bus_handle_t bus) {
  i2c_device_config_t dev = {};
  dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev.device_address = kAddress;
  dev.scl_speed_hz = kBusSpeedHz;
  esp_err_t err = i2c_master_bus_add_device(bus, &dev, &device_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "adding I2C device 0x%02x failed: %s", kAddress, esp_err_to_name(err));
    return err;
  }

  if ((err = select_page(Page::Control)) != ESP_OK) return err;
  if ((err = write(kSoftwareReset, kResetValue)) != ESP_OK) return err;
  esp_rom_delay_us(kResetSettleUs);
  for (const auto& w : kPage0Setup) {
    if ((err = write(w.reg, w.value)) != ESP_OK) return err;
  }
  if ((err = select_page(Page::Analog)) != ESP_OK) return err;
  for (const auto& w : kPage1Setup) {
    if ((err = write(w.reg, w.value)) != ESP_OK) return err;
  }
  drivers_on_at_us_ = esp_timer_get_time();
  return ESP_OK;
}

esp_err_t Aic3204::power_up() {
  if (!device_) return ESP_ERR_INVALID_STATE;
  const int64_t wait_us = drivers_on_at_us_ + kSoftStartUs - esp_timer_get_time();
  if (wait_us > 0) vTaskDelay(pdMS_TO_TICKS(wait_us / kUsPerMs));
  esp_err_t err = select_page(Page::Control);
  if (err == ESP_OK) err = write(kDacSetup1, kDacChannelsOn);
  if (err != ESP_OK) return err;
  ESP_LOGI(TAG, "ready");
  log_status();
  return ESP_OK;
}

void Aic3204::log_status() {
  uint8_t flags1 = 0;
  uint8_t flags2 = 0;
  if (select_page(Page::Control) != ESP_OK || read(kDacFlags1, flags1) != ESP_OK || read(kDacFlags2, flags2) != ESP_OK) return;
  ESP_LOGI(TAG, "power flags 0x%02x, gain flags 0x%02x", flags1, flags2);
}

esp_err_t Aic3204::set_volume(uint8_t percent) {
  if (!device_) return ESP_ERR_INVALID_STATE;
  esp_err_t err = select_page(Page::Control);
  if (err != ESP_OK) return err;
  if (percent == 0) return write(kDacSetup2, kDacMuted);
  const int clamped = percent > kPercentMax ? kPercentMax : percent;
  const auto step =
      static_cast<int8_t>(kVolumeMinStep + clamped * (kVolumeMaxStep - kVolumeMinStep) / kPercentMax);
  if ((err = write(kDacLeftVolume, static_cast<uint8_t>(step))) != ESP_OK) return err;
  if ((err = write(kDacRightVolume, static_cast<uint8_t>(step))) != ESP_OK) return err;
  return write(kDacSetup2, kDacUnmuted);
}

esp_err_t Aic3204::select_page(Page page) { return write(kPageSelect, static_cast<uint8_t>(page)); }

esp_err_t Aic3204::read(uint8_t reg, uint8_t& value) {
  const esp_err_t err = i2c_master_transmit_receive(device_, &reg, sizeof(reg), &value, sizeof(value), kTimeoutMs);
  if (err != ESP_OK) ESP_LOGE(TAG, "reading register 0x%02x failed: %s", reg, esp_err_to_name(err));
  return err;
}

esp_err_t Aic3204::write(uint8_t reg, uint8_t value) {
  const uint8_t bytes[] = {reg, value};
  const esp_err_t err = i2c_master_transmit(device_, bytes, sizeof(bytes), kTimeoutMs);
  if (err != ESP_OK) ESP_LOGE(TAG, "writing register 0x%02x failed: %s", reg, esp_err_to_name(err));
  return err;
}

}  // namespace nabu
