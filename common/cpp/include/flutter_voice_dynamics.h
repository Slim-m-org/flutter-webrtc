#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_VOICE_DYNAMICS_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_VOICE_DYNAMICS_H_

#include <atomic>

namespace flutter_webrtc_plugin {

// Evens out loud sounds on the microphone: a soft-knee compressor that pulls
// shouts down towards normal speech, then a limiter that keeps every sample
// under a ceiling so plosives and bangs never clip or blast listeners.
// Quiet and normal speech pass through unchanged.
class VoiceDynamics {
 public:
  static VoiceDynamics* Instance();

  void Configure(bool enabled) { enabled_.store(enabled); }
  bool Enabled() const { return enabled_.load(); }

  // Forget the levels, as when capture restarts.
  void Restart() { was_enabled_ = false; }

  // One 10ms frame of mono samples on the int16 scale, in place.
  void Process(float* buffer, int num_frames);

 private:
  VoiceDynamics() = default;

  void Reset(int sample_rate_hz);

  std::atomic<bool> enabled_{false};

  // Only touched on the audio thread.
  bool was_enabled_ = false;
  int sample_rate_hz_ = 0;
  float attack_coeff_ = 0.0f;
  float release_coeff_ = 0.0f;
  float limiter_release_coeff_ = 0.0f;
  // Mean square of the normalized signal.
  float envelope_ = 0.0f;
  float limiter_gain_ = 1.0f;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_VOICE_DYNAMICS_H_
