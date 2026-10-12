// The processed microphone signal from the XMOS: I2S on GPIO 13/14/15 with the
// XMOS as clock master, 16 kHz, two 32-bit slots. Channel 0 (left) carries the
// fully processed speech; channel 1 is dropped.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include "driver/i2s_std.h"

namespace nabu {

class XmosMic final : public hg::AudioIn {
 public:
  static constexpr uint32_t kRate = 16000;

  bool begin();
  bool start(uint32_t sample_rate) override;
  void stop() override;

 private:
  static constexpr size_t kChunkFrames = 320;  // 20 ms
  static constexpr size_t kSlots = 2;

  static void task(void* arg);
  void capture_chunk();
  i2s_chan_handle_t rx_ = nullptr;
  std::atomic<bool> capturing_{false};
  // Per capture, for the log line on stop(): shows whether the XMOS sends.
  std::atomic<uint32_t> captured_frames_{0};
  std::atomic<uint32_t> failed_reads_{0};
  std::atomic<int32_t> peak_{0};
  int32_t raw_[kChunkFrames * kSlots] = {};
  int16_t pcm_[kChunkFrames] = {};
};

}  // namespace nabu
