// The Voice PE's ring of 12 RGB LEDs on GPIO 21, powered through GPIO 45.
#pragma once

#include "led_patterns.hpp"
#include "led_strip.h"

namespace nabu {

class LedRing {
 public:
  // Powers the ring and starts its RMT channel. False when that fails; the
  // device then runs without the ring.
  bool begin();
  // Sends the frame, unless the ring already shows it.
  void show(const RingFrame& frame);

 private:
  led_strip_handle_t strip_ = nullptr;
  RingFrame shown_{};
  bool shown_valid_ = false;
};

}  // namespace nabu
