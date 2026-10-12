// Control of the Voice PE's XMOS XU316 audio processor: reset on GPIO 4 and
// its configuration service over I2C. The XMOS runs the processing pipeline
// (echo cancellation, interference cancellation, noise suppression, gain)
// and clocks the processed microphone audio out over I2S.
//
// Register numbers follow the XMOS sln_voice ffva firmware that ships on the
// device; DFU firmware updates are not implemented.
#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace nabu {

class XmosControl {
 public:
  // Pulses the reset line. The XMOS then needs a few seconds to boot.
  esp_err_t reset();
  // Waits until the XMOS has booted, then selects the processing stage of
  // each output channel and reads it back to check the link.
  esp_err_t configure(i2c_master_bus_handle_t bus);

 private:
  esp_err_t write_stage(uint8_t channel_register, uint8_t stage);
  esp_err_t read_stage(uint8_t channel_register, uint8_t& stage);
  i2c_master_dev_handle_t device_ = nullptr;
  int64_t reset_at_us_ = 0;
};

}  // namespace nabu
