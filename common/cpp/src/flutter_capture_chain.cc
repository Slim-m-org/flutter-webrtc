#include "flutter_capture_chain.h"

#include "flutter_input_gate.h"
#include "flutter_voice_dynamics.h"

namespace flutter_webrtc_plugin {

CaptureChain* CaptureChain::Instance() {
  static CaptureChain* chain = new CaptureChain();
  return chain;
}

void CaptureChain::Initialize(int sample_rate_hz, int num_channels) {
  InputGate::Instance()->Initialize(sample_rate_hz, num_channels);
}

void CaptureChain::Process(int num_bands, int num_frames, int buffer_size,
                           float* buffer) {
  InputGate::Instance()->Process(num_bands, num_frames, buffer_size, buffer);
  VoiceDynamics::Instance()->Process(buffer, num_frames);
}

void CaptureChain::Reset(int new_rate) {
  InputGate::Instance()->Reset(new_rate);
}

}  // namespace flutter_webrtc_plugin
