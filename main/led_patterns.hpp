// What the LED ring shows for each state of the core, as a pure function of
// the state and the time: no ESP-IDF here, so it can be tested on the host.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hg/ui.hpp"

namespace nabu {

struct Rgb {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  bool operator==(const Rgb& other) const { return r == other.r && g == other.g && b == other.b; }
  bool operator!=(const Rgb& other) const { return !(*this == other); }
};

constexpr size_t kRingSize = 12;

// Logical ring positions, increasing clockwise. LedRing maps them onto the
// physical LED chain.
using RingFrame = std::array<Rgb, kRingSize>;

RingFrame ring_frame(hg::Screen screen, uint32_t now_ms);

}  // namespace nabu
