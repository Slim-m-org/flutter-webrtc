#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_

#include <atomic>
#include <memory>

#include "flutter_rate_converter.h"

struct DenoiseState;

namespace flutter_webrtc_plugin {

// Stronger noise suppression with rnnoise, a small recurrent network that
// removes keyboards, fans and room noise that libwebrtc's own suppressor
// lets through. rnnoise only runs at 48 kHz, so any other capture rate is
// resampled up for it and back down; 48 kHz skips both conversions.
class NoiseSuppressor {
 public:
  static constexpr int kMaxChannels = 2;
  // Returned by Process when the frame was not denoised.
  static constexpr float kNoSpeechProbability = -1.0f;

  static NoiseSuppressor* Instance();

  // Public so tests can build isolated instances.
  NoiseSuppressor();
  ~NoiseSuppressor();
  NoiseSuppressor(const NoiseSuppressor&) = delete;
  NoiseSuppressor& operator=(const NoiseSuppressor&) = delete;

  void Configure(bool enabled) { enabled_.store(enabled); }
  bool Enabled() const { return enabled_.load(); }

  // Forget the network and resampler history, as when capture restarts.
  void Restart() { was_active_ = false; }

  // Whether the last frame was actually denoised.
  bool Active() const { return active_.load(); }

  // One 10ms frame of mono samples on the int16 scale, in place. Returns
  // rnnoise's speech probability for it, or kNoSpeechProbability if the
  // frame was left alone.
  float Process(float* buffer, int num_frames);

  // The same for one planar frame per channel; returns the loudest-speech
  // channel's probability.
  float ProcessPlanar(float* const* channels, int num_channels,
                      int num_frames);

 private:
  struct Channel;

  bool Prepare(int num_channels, int num_frames);
  float ProcessChannel(Channel& channel, float* buffer, int num_frames);

  std::atomic<bool> enabled_{false};
  std::atomic<bool> active_{false};

  // Only touched on the audio thread.
  std::unique_ptr<Channel> channels_[kMaxChannels];
  int rate_hz_ = 0;
  bool was_active_ = false;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
