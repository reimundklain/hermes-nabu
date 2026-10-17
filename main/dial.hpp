// The rotary dial around the center button (GPIO 16/18), decoded in hardware
// by the pulse counter and polled from the app task.
#pragma once

#include "driver/pulse_cnt.h"

namespace nabu {

class Dial {
 public:
  // Starts the pulse counter. False when that fails; poll() then reports no
  // turns.
  bool begin();
  // Call every ~10 ms. Detents turned since the last call, positive clockwise.
  int poll();

 private:
  pcnt_unit_handle_t unit_ = nullptr;
  int reported_count_ = 0;  // the count up to the last reported detent
};

}  // namespace nabu
