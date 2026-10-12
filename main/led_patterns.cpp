// Colors and animations follow the stock Voice PE firmware
// (home-assistant-voice.yaml), so the device looks familiar.
#include "led_patterns.hpp"

namespace nabu {
namespace {

constexpr Rgb kAccent{24, 187, 242};
constexpr Rgb kWarmWhite{255, 227, 181};
constexpr Rgb kAmber{255, 140, 0};
constexpr Rgb kRed{255, 0, 0};

constexpr uint8_t kFull = 255;
constexpr uint8_t kTrailNear = 192;
constexpr uint8_t kTrailFar = 128;
constexpr uint8_t kBreatheFloor = 16;

constexpr uint32_t kSpinFastStepMs = 50;
constexpr uint32_t kSpinSlowStepMs = 100;
constexpr uint32_t kBreatheSlowMs = 2000;
constexpr uint32_t kBreatheFastMs = 500;
constexpr uint32_t kThinkingPulseMs = 400;

enum class Direction : uint8_t { Clockwise, Anticlockwise };

Rgb scaled(Rgb color, uint8_t level) {
  return {static_cast<uint8_t>(color.r * level / kFull), static_cast<uint8_t>(color.g * level / kFull),
          static_cast<uint8_t>(color.b * level / kFull)};
}

// Rises from 0 to 255 over the first half of the period, then falls back.
uint8_t triangle(uint32_t now_ms, uint32_t period_ms) {
  const uint32_t half = period_ms / 2;
  const uint32_t phase = now_ms % period_ms;
  const uint32_t rising = phase < half ? phase : period_ms - phase;
  return static_cast<uint8_t>(rising * kFull / half);
}

RingFrame solid(Rgb color) {
  RingFrame frame;
  frame.fill(color);
  return frame;
}

RingFrame breathe(Rgb color, uint32_t now_ms, uint32_t period_ms) {
  const uint8_t level = kBreatheFloor + triangle(now_ms, period_ms) * (kFull - kBreatheFloor) / kFull;
  return solid(scaled(color, level));
}

// Two opposite heads with fading trails, like the stock firmware's spin.
RingFrame spin(Rgb color, uint32_t now_ms, uint32_t step_ms, Direction direction) {
  constexpr size_t kOpposite = kRingSize / 2;
  const size_t step = (now_ms / step_ms) % kRingSize;
  const size_t head = direction == Direction::Clockwise ? step : kRingSize - 1 - step;
  const size_t behind = direction == Direction::Clockwise ? kRingSize - 1 : 1;
  RingFrame frame{};
  for (size_t offset : {size_t{0}, kOpposite}) {
    const size_t lead = (head + offset) % kRingSize;
    frame[lead] = color;
    frame[(lead + behind) % kRingSize] = scaled(color, kTrailNear);
    frame[(lead + 2 * behind) % kRingSize] = scaled(color, kTrailFar);
  }
  return frame;
}

// Two opposite LEDs pulsing in place.
RingFrame thinking(Rgb color, uint32_t now_ms) {
  constexpr size_t kFirst = 0;
  constexpr size_t kSecond = kRingSize / 2;
  const Rgb pulse = scaled(color, kFull - triangle(now_ms, kThinkingPulseMs));
  RingFrame frame{};
  frame[kFirst] = pulse;
  frame[kSecond] = pulse;
  return frame;
}

}  // namespace

RingFrame ring_frame(hg::Screen screen, uint32_t now_ms) {
  switch (screen) {
    case hg::Screen::Boot: return solid(kWarmWhite);
    case hg::Screen::Offline: return breathe(kWarmWhite, now_ms, kBreatheSlowMs);
    case hg::Screen::Connecting: return breathe(kAccent, now_ms, kBreatheSlowMs);
    case hg::Screen::Pairing: return breathe(kAmber, now_ms, kBreatheSlowMs);
    case hg::Screen::Listening: return spin(kAccent, now_ms, kSpinFastStepMs, Direction::Clockwise);
    case hg::Screen::Thinking: return thinking(kAccent, now_ms);
    case hg::Screen::Responding: return spin(kAccent, now_ms, kSpinFastStepMs, Direction::Anticlockwise);
    case hg::Screen::Error: return breathe(kRed, now_ms, kBreatheFastMs);
    case hg::Screen::Prompt: return solid(kAccent);
    case hg::Screen::Updating: return spin(kWarmWhite, now_ms, kSpinSlowStepMs, Direction::Clockwise);
    // Ready is dark, as on the stock firmware. The rest are screen-only
    // overlays, or menus a device without a display never opens.
    case hg::Screen::Ready:
    case hg::Screen::Card:
    case hg::Screen::Image:
    case hg::Screen::Settings:
    case hg::Screen::Setup:
      return RingFrame{};
  }
  return RingFrame{};
}

}  // namespace nabu
