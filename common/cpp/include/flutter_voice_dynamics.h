#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_VOICE_DYNAMICS_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_VOICE_DYNAMICS_H_

#include <atomic>

namespace flutter_webrtc_plugin {

// Evens out loud sounds on the microphone: a soft-knee compressor that pulls
// shouts down towards normal speech, then a limiter that keeps every sample
// under a ceiling so plosives and bangs never clip or blast listeners.
// Quiet and normal speech pass through unchanged.
//
// With the leveler on it also lifts quiet speech towards a target level,
// slowly and only on frames the suppressor is sure are speech, so a chewing
// or swallowing sound in a pause is sent at the gain the voice earned and
// never a louder one. It stands in for libwebrtc's auto gain, which runs
// before the suppressor and lifts quiet non-speech into the band the
// suppressor keeps.
class VoiceDynamics {
 public:
  static VoiceDynamics* Instance();

  void Configure(bool enabled) { enabled_.store(enabled); }
  bool Enabled() const { return enabled_.load(); }

  void ConfigureLeveler(bool enabled) { leveler_.store(enabled); }
  bool LevelerEnabled() const { return leveler_.load(); }

  // The lift the leveler applies right now, 0 to its ceiling, for the log.
  float LevelerGainDb() const { return leveler_gain_db_.load(); }

  // Forget the levels, as when capture restarts.
  void Restart() { was_enabled_ = false; }

  // One 10ms frame of mono samples on the int16 scale, in place.
  // `speech_probability` is the suppressor's for this frame, or negative when
  // there is none; the leveler adapts only on confident speech.
  void Process(float* buffer, int num_frames, float speech_probability);
  void Process(float* buffer, int num_frames) {
    Process(buffer, num_frames, -1.0f);
  }

 private:
  VoiceDynamics() = default;

  void Reset(int sample_rate_hz);
  void AdaptLeveler(const float* buffer, int num_frames,
                    float speech_probability);

  std::atomic<bool> enabled_{false};
  std::atomic<bool> leveler_{false};
  std::atomic<float> leveler_gain_db_{0.0f};

  // Only touched on the audio thread.
  bool was_enabled_ = false;
  int sample_rate_hz_ = 0;
  float attack_coeff_ = 0.0f;
  float release_coeff_ = 0.0f;
  float limiter_release_coeff_ = 0.0f;
  // Mean square of the normalized signal.
  float envelope_ = 0.0f;
  float limiter_gain_ = 1.0f;
  float gain_db_ = 0.0f;
  // The linear gain the last frame ended on, so a change ramps across the next frame.
  float applied_gain_ = 1.0f;
  int speech_run_ = 0;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_VOICE_DYNAMICS_H_
