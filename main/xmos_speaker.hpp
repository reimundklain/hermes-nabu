// Playback through the XMOS to the AIC3204 DAC and the speaker amplifier:
// I2S on GPIO 7/8/10 with the XMOS as clock master, 48 kHz, two 32-bit slots.
// The XMOS also takes this signal as its echo-cancellation reference.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include "aic3204.hpp"
#include "driver/i2s_std.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"

namespace nabu {

class XmosSpeaker final : public hg::AudioOut {
 public:
  static constexpr uint32_t kRate = 48000;
  // The loudest level it plays, in percent; louder requests play at this one.
  static constexpr uint8_t kMaxVolume = 80;

  // Starts the always-running I2S output and turns the amplifier on. The DAC
  // must be powered up already.
  bool begin(Aic3204& dac);
  bool begin(uint32_t sample_rate) override;
  void write(const int16_t* samples, size_t count) override;
  void end() override;
  void abort() override;
  bool busy() const override;
  void set_volume(uint8_t percent) override;

 private:
  static constexpr size_t kChunkFrames = 480;  // 10 ms
  static constexpr size_t kSlots = 2;

  static void task(void* arg);
  void play_chunk();
  void flush_if_requested();
  bool wait_for_flush();
  bool prebuffered() const;
  Aic3204* dac_ = nullptr;
  i2s_chan_handle_t tx_ = nullptr;
  StreamBufferHandle_t buffer_ = nullptr;
  std::atomic<bool> open_{false};
  std::atomic<bool> draining_{false};
  // Only the playback task resets the buffer: abort() asks for a flush, and the
  // task counts it done and gives `flushed_`.
  std::atomic<uint32_t> flush_requests_{0};
  std::atomic<uint32_t> flushes_done_{0};
  SemaphoreHandle_t flushed_ = nullptr;
  // Playback task only: false until a stream has prebuffered, and again after
  // the buffer ran dry.
  bool playing_ = false;
  // Per stream, for the log line once it has played out: shows whether audio
  // arrives from Hermes and leaves for the XMOS.
  std::atomic<uint32_t> played_frames_{0};
  std::atomic<uint32_t> underruns_{0};
  std::atomic<uint32_t> failed_writes_{0};
  std::atomic<int32_t> peak_{0};
  int16_t mono_[kChunkFrames] = {};
  int32_t stereo_[kChunkFrames * kSlots] = {};
};

}  // namespace nabu
