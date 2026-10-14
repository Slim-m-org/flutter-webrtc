#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_INPUT_GATE_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_INPUT_GATE_H_

#include <atomic>

#include "rtc_audio_processing.h"

namespace flutter_webrtc_plugin {

// A noise gate on the microphone, after echo cancellation and noise
// suppression: frames quieter than the threshold go out as silence. It also
// meters the capture level, so the app can show it and light the local
// speaking indicator without waiting for the SFU.
class InputGate : public libwebrtc::RTCAudioProcessing::CustomProcessing {
 public:
  // One per process: the audio processing module keeps a raw pointer to it
  // for as long as the factory lives, so it is never freed.
  static InputGate* Instance();

  void Configure(bool enabled, float threshold_db);

  // The metered level in dBFS, from -100 (silence) to 0.
  float LevelDb() const { return level_db_.load(); }

  // Whether the level crossed the threshold within the hold time. Tracked
  // while the gate is disabled too, for the local speaking indicator.
  bool VoiceDetected() const { return voice_.load(); }

  void Initialize(int sample_rate_hz, int num_channels) override;
  void Process(int num_bands, int num_frames, int buffer_size,
               float* buffer) override;
  void Reset(int new_rate) override;
  void Release() override {}

 private:
  InputGate() = default;

  std::atomic<bool> enabled_{false};
  std::atomic<float> threshold_db_{-50.0f};
  std::atomic<float> level_db_{-100.0f};
  std::atomic<bool> voice_{false};

  // Only touched on the audio thread.
  int hold_frames_left_ = 0;
  float gain_ = 1.0f;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_INPUT_GATE_H_
