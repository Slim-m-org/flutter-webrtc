#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_

#include <atomic>

struct DenoiseState;

namespace flutter_webrtc_plugin {

// Stronger noise suppression with rnnoise, a small recurrent network that
// removes keyboards, fans and room noise that libwebrtc's own suppressor
// lets through. It needs 48 kHz frames; at any other capture rate it passes
// audio through and reports itself inactive.
class NoiseSuppressor {
 public:
  static NoiseSuppressor* Instance();

  void Configure(bool enabled) { enabled_.store(enabled); }
  bool Enabled() const { return enabled_.load(); }

  // Whether the last frame was actually denoised.
  bool Active() const { return active_.load(); }

  // One 10ms frame of mono samples on the int16 scale, in place.
  void Process(float* buffer, int num_frames);

 private:
  NoiseSuppressor() = default;

  std::atomic<bool> enabled_{false};
  std::atomic<bool> active_{false};

  // Only touched on the audio thread.
  DenoiseState* state_ = nullptr;
  bool was_active_ = false;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
