#include "flutter_capture_chain.h"

#include "flutter_input_gate.h"
#include "flutter_noise_suppressor.h"
#include "flutter_voice_dynamics.h"

#include <algorithm>
#include <cmath>

namespace flutter_webrtc_plugin {

namespace {

// Well past int16 full scale, so only a corrupt frame is touched.
constexpr float kSampleLimit = 4.0f * 32768.0f;

// A single NaN would otherwise sit in the recurrent and envelope state of
// every later stage and keep the output broken until a restart.
void ScrubNonFinite(float* buffer, int num_frames) {
  for (int i = 0; i < num_frames; ++i) {
    const float v = buffer[i];
    if (!(std::fabs(v) <= kSampleLimit)) {
      buffer[i] = std::isnan(v) ? 0.0f : std::copysign(kSampleLimit, v);
    }
  }
}

}  // namespace

CaptureChain* CaptureChain::Instance() {
  static CaptureChain* chain = new CaptureChain();
  return chain;
}

void CaptureChain::Initialize(int sample_rate_hz, int num_channels) {
  InputGate::Instance()->Initialize(sample_rate_hz, num_channels);
  NoiseSuppressor::Instance()->Restart();
  VoiceDynamics::Instance()->Restart();
}

void CaptureChain::Process(int /*num_bands*/, int num_frames,
                           int /*buffer_size*/,
                           float* buffer) {
  if (buffer == nullptr || num_frames <= 0) return;
  ScrubNonFinite(buffer, num_frames);
  const float speech =
      NoiseSuppressor::Instance()->Process(buffer, num_frames);
  InputGate::Instance()->ProcessWithSpeech(num_frames, buffer, speech);
  VoiceDynamics::Instance()->Process(buffer, num_frames);
}

void CaptureChain::Reset(int new_rate) {
  InputGate::Instance()->Reset(new_rate);
  NoiseSuppressor::Instance()->Restart();
  VoiceDynamics::Instance()->Restart();
}

}  // namespace flutter_webrtc_plugin
