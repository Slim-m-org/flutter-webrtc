#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_MIC_SAMPLE_TAP_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_MIC_SAMPLE_TAP_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "flutter_rate_converter.h"

namespace flutter_webrtc_plugin {

// Keeps the first seconds of the microphone exactly as the capture chain
// receives it, before any suppression, gate or dynamics, as 48 kHz mono
// 16-bit samples for the app to write out. Nothing leaves the process.
class MicSampleTap {
 public:
  static constexpr int kRateHz = 48000;

  // Never freed, for the reason InputGate::Instance gives.
  static MicSampleTap* Instance();

  // Public so tests can build isolated instances.
  MicSampleTap() = default;

  // Clears what was kept and records up to max_seconds more.
  void Start(int max_seconds);
  void Stop();
  bool Armed() const { return armed_.load(); }

  // One 10 ms frame in the capture's own rate and in 16-bit full scale, from
  // the audio thread. A rate that cannot be converted to 48 kHz is skipped.
  void Feed(const float* frame, int num_frames);

  // Everything kept since the last call, which the caller now owns.
  std::vector<int16_t> Take();

  // Frames skipped because they could not be brought to 48 kHz.
  int SkippedFrames() const { return skipped_.load(); }

  // True once max_seconds have been kept and nothing more is recorded.
  bool Full() const { return full_.load(); }

 private:
  std::atomic<bool> armed_{false};
  std::atomic<bool> full_{false};
  std::atomic<int> skipped_{0};
  std::mutex mutex_;
  std::vector<int16_t> samples_;
  size_t limit_ = 0;
  size_t kept_ = 0;
  RateConverter converter_;
  int converter_rate_hz_ = 0;
  std::vector<float> converted_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_MIC_SAMPLE_TAP_H_
