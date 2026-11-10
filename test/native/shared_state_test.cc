#include <vector>

#include "check.h"
#include "flutter_capture_chain.h"
#include "flutter_input_gate.h"
#include "flutter_noise_suppressor.h"
#include "flutter_voice_dynamics.h"

namespace {

using flutter_webrtc_plugin::CaptureChain;
using flutter_webrtc_plugin::InputGate;
using flutter_webrtc_plugin::NoiseSuppressor;
using flutter_webrtc_plugin::VoiceDynamics;

constexpr int kRate = 48000;
constexpr float kDefaultThresholdDb = -50.0f;

// The stages are process singletons, so every test starts from their defaults.
void ResetSharedState() {
  NoiseSuppressor::Instance()->ConfigureStrongest(false);
  NoiseSuppressor::Instance()->Configure(false);
  InputGate::Instance()->Configure(false, kDefaultThresholdDb);
  VoiceDynamics::Instance()->Configure(false);
  VoiceDynamics::Instance()->ConfigureLeveler(false);
  CaptureChain::Instance()->Initialize(kRate, 1);
}

[[maybe_unused]] const bool kInstalled = (SetBeforeEach(ResetSharedState), true);

std::vector<float> Frame(float level) { return std::vector<float>(kRate / 100, level); }

}  // namespace

// Written to run first: it leaves every stage on and the gate open.
TEST(a_test_may_leave_every_shared_stage_switched_on) {
  NoiseSuppressor::Instance()->Configure(true);
  InputGate::Instance()->Configure(true, kDefaultThresholdDb);
  VoiceDynamics::Instance()->Configure(true);
  VoiceDynamics::Instance()->ConfigureLeveler(true);
  for (int i = 0; i < 10; ++i) {
    std::vector<float> loud = Frame(8000.0f);
    InputGate::Instance()->ProcessWithSpeech(kRate / 100, loud.data(), 1.0f);
    VoiceDynamics::Instance()->Process(loud.data(), kRate / 100, 1.0f);
  }
  CHECK(InputGate::Instance()->VoiceDetected());
}

TEST(the_next_test_starts_from_the_defaults) {
  CHECK(!NoiseSuppressor::Instance()->Enabled());
  CHECK(!NoiseSuppressor::Instance()->StrongestEnabled());
  CHECK(!VoiceDynamics::Instance()->Enabled());
  CHECK(!VoiceDynamics::Instance()->LevelerEnabled());
  CHECK(InputGate::Instance()->TakePeakDb() <= -100.0f);
  float last = 0.0f;
  for (int i = 0; i < 4; ++i) {
    std::vector<float> quiet = Frame(100.0f);
    CaptureChain::Instance()->Process(1, kRate / 100, kRate / 100, quiet.data());
    last = quiet[0];
  }
  CHECK(last == 100.0f);
}
