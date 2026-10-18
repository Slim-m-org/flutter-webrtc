#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_INPUT_GATE_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_INPUT_GATE_H_

#include <atomic>

#include "rtc_audio_processing.h"

namespace flutter_webrtc_plugin {

// A noise gate on the microphone, after echo cancellation and noise
// suppression: frames quieter than the threshold go out as silence. It also
// meters the level of the frames it decides on, so the app can show it and
// light the local speaking indicator without waiting for the SFU.
class InputGate : public libwebrtc::RTCAudioProcessing::CustomProcessing {
 public:
  // One per process: the audio processing module keeps a raw pointer to it
  // for as long as the factory lives, so it is never freed.
  static InputGate* Instance();

  void Configure(bool enabled, float threshold_db);

  // The loudest frame level in dBFS since the last call, from -100 (silence)
  // to 0: the same per-frame level the threshold is compared with, so a
  // meter drawn from it crosses the threshold exactly when the gate counts a
  // frame as loud enough. Single consumer: only InputGateMonitor polls it,
  // a second caller would steal peaks from the meter.
  float TakePeakDb() { return peak_db_.exchange(-100.0f); }

  // Whether the level crossed the threshold within the hold time. Tracked
  // while the gate is disabled too, for the local speaking indicator.
  bool VoiceDetected() const { return voice_.load(); }

  void Initialize(int sample_rate_hz, int num_channels) override;
  void Process(int num_bands, int num_frames, int buffer_size,
               float* buffer) override;
  void Reset(int new_rate) override;
  void Release() override {}

  // Process with rnnoise's speech probability for the frame, so keyboard
  // clicks and other loud non-speech can not open the gate. Pass a negative
  // probability when there is none, which behaves like plain Process.
  void ProcessWithSpeech(int num_frames, float* buffer,
                         float speech_probability);

 private:
  InputGate() = default;

  std::atomic<bool> enabled_{false};
  std::atomic<float> threshold_db_{-50.0f};
  std::atomic<float> peak_db_{-100.0f};
  std::atomic<bool> voice_{false};

  // Only touched on the audio thread.
  int hold_frames_left_ = 0;
  int loud_run_frames_ = 0;
  float gain_ = 1.0f;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_INPUT_GATE_H_
