// The hardware mute switch on the side of the Voice PE (GPIO 3, high when
// muted), polled from the app task with a short debounce.
#pragma once

#include <cstdint>

namespace nabu {

class MuteSwitch {
 public:
  // Configures the pin and takes its current position as the starting state.
  // On failure the switch reports muted for good.
  bool begin();
  // Call every ~10 ms. True when the debounced position has just changed.
  bool poll();
  bool muted() const { return muted_; }

 private:
  bool readable_ = false;
  bool muted_ = false;
  uint8_t stable_ = 0;
};

}  // namespace nabu
