#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_CAPTURE_CHAIN_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_CAPTURE_CHAIN_H_

#include "rtc_audio_processing.h"

namespace flutter_webrtc_plugin {

// The audio processing module takes a single capture post-processor, so this
// runs every microphone stage in order: the noise gate, then the dynamics.
class CaptureChain : public libwebrtc::RTCAudioProcessing::CustomProcessing {
 public:
  // Never freed, for the reason InputGate::Instance gives.
  static CaptureChain* Instance();

  void Initialize(int sample_rate_hz, int num_channels) override;
  void Process(int num_bands, int num_frames, int buffer_size,
               float* buffer) override;
  void Reset(int new_rate) override;
  void Release() override {}

 private:
  CaptureChain() = default;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_CAPTURE_CHAIN_H_
