// The TI TLV320AIC3204 DAC behind the XMOS: I2C configuration, power-up and
// digital volume. The XMOS supplies its clocks for 48 kHz playback.
//
// Register values follow the TLV320AIC3204 Application Reference Guide
// (SLAA557); the stock Voice PE firmware uses the same setup.
#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace nabu {

class Aic3204 {
 public:
  // Resets the DAC, sets clocks, interface and analog routing, and powers the
  // output drivers. Their soft start takes a while; power_up() finishes.
  esp_err_t begin(i2c_master_bus_handle_t bus);
  // Waits for the drivers' soft start, then powers the DAC channels on.
  esp_err_t power_up();
  // 0 mutes; 1..100 maps linearly in dB onto the DAC's digital volume.
  esp_err_t set_volume(uint8_t percent);
  // Logs the DAC's power flags, to confirm the channels and drivers are on.
  void log_status();

 private:
  // Register pages: control and DAC on page 0, analog and power on page 1.
  enum class Page : uint8_t { Control = 0, Analog = 1 };

  esp_err_t write(uint8_t reg, uint8_t value);
  esp_err_t read(uint8_t reg, uint8_t& value);
  esp_err_t select_page(Page page);
  i2c_master_dev_handle_t device_ = nullptr;
  int64_t drivers_on_at_us_ = 0;
};

}  // namespace nabu
