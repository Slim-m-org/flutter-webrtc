#include "flutter_capture_chain_api.h"

#include "flutter_capture_chain.h"
#include "flutter_input_gate.h"
#include "flutter_noise_suppressor.h"
#include "flutter_voice_dynamics.h"

using flutter_webrtc_plugin::CaptureChain;
using flutter_webrtc_plugin::InputGate;
using flutter_webrtc_plugin::NoiseSuppressor;
using flutter_webrtc_plugin::VoiceDynamics;

namespace {

constexpr int kMaxFrameSamples = 4800;
float g_frame[kMaxFrameSamples];

}  // namespace

extern "C" {

void slimm_chain_initialize(int sample_rate_hz, int num_channels) {
  CaptureChain::Instance()->Initialize(sample_rate_hz, num_channels);
}

int slimm_chain_process(float* buffer, int num_frames) {
  if (num_frames > kMaxFrameSamples) return 0;
  CaptureChain::Instance()->Process(1, num_frames, num_frames, buffer);
  return 1;
}

void slimm_chain_configure(int gate_enabled, float threshold_db, int dynamics,
                           int level_after_suppression,
                           int strong_noise_suppression) {
  InputGate::Instance()->Configure(gate_enabled != 0, threshold_db);
  VoiceDynamics::Instance()->Configure(dynamics != 0);
  VoiceDynamics::Instance()->ConfigureLeveler(level_after_suppression != 0);
  NoiseSuppressor::Instance()->Configure(strong_noise_suppression != 0);
}

float slimm_chain_take_peak_db(void) {
  return InputGate::Instance()->TakePeakDb();
}

int slimm_chain_voice(void) {
  return InputGate::Instance()->VoiceDetected() ? 1 : 0;
}

int slimm_chain_dynamics(void) {
  return VoiceDynamics::Instance()->Enabled() ? 1 : 0;
}

float slimm_chain_leveler_gain_db(void) {
  return VoiceDynamics::Instance()->LevelerGainDb();
}

int slimm_chain_strong_active(void) {
  return NoiseSuppressor::Instance()->Active() ? 1 : 0;
}

float* slimm_chain_frame_buffer(void) { return g_frame; }

int slimm_chain_max_frame_samples(void) { return kMaxFrameSamples; }

}  // extern "C"
