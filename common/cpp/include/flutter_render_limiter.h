#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_LIMITER_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_LIMITER_H_

#include <atomic>

#include "rtc_audio_processing.h"

namespace flutter_webrtc_plugin {

// Applies the part of a per-person volume above 100% to the mixed playout, in
// float, through a soft knee: quiet and normal speech scale exactly, and only
// peaks past the knee bend towards full scale, so nothing ever hard-clips.
// With a boost of 1 it leaves the samples untouched.
class RenderLimiter : public libwebrtc::RTCAudioProcessing::CustomProcessing {
 public:
  // Never freed, for the reason InputGate::Instance gives.
  static RenderLimiter* Instance();

  // Multiplier applied to the whole mix; 1 or more.
  void SetBoost(float boost);
  float Boost() const { return boost_.load(); }

  // Samples on the int16 scale, in place.
  static void Apply(float* buffer, int count, float boost);

  void Initialize(int sample_rate_hz, int num_channels) override;
  void Process(int num_bands, int num_frames, int buffer_size,
               float* buffer) override;
  void Reset(int new_rate) override {}
  void Release() override {}

 private:
  RenderLimiter() = default;

  std::atomic<float> boost_{1.0f};
  std::atomic<int> channels_{1};
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_LIMITER_H_
