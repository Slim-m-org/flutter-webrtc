#include <cmath>
#include <cstdio>
#include <vector>

#include "check.h"
#include "flutter_input_gate.h"

namespace {

using flutter_webrtc_plugin::InputGate;

constexpr int kFrame = 160;
constexpr float kThresholdDb = -50.0f;

float DbToAmplitude(float db) { return 32768.0f * std::pow(10.0f, db / 20.0f); }

// One 10 ms frame of a steady tone-like signal at the given level.
std::vector<float> Frame(float db) {
  std::vector<float> f(kFrame);
  for (int i = 0; i < kFrame; ++i) {
    f[i] = DbToAmplitude(db) * 1.41421356f *
           static_cast<float>(std::sin(2.0 * M_PI * 200.0 * i / 16000.0));
  }
  return f;
}

// Feeds one frame and returns whether the gate counts it as voice.
bool Feed(InputGate& gate, float db, float speech) {
  std::vector<float> f = Frame(db);
  gate.ProcessWithSpeech(kFrame, f.data(), speech);
  return gate.VoiceDetected();
}

// Frames of silence until the gate closes; capped.
int FramesUntilClosed(InputGate& gate, float db, float speech) {
  for (int n = 1; n <= 100; ++n) {
    if (!Feed(gate, db, speech)) return n;
  }
  return 100;
}

}  // namespace

// The gate is a process singleton, so tests reset it through Configure.
TEST(gate_ignores_a_single_loud_click_without_speech) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  for (int i = 0; i < 5; ++i) Feed(gate, -80.0f, 0.0f);
  CHECK(!Feed(gate, -20.0f, 0.05f));
  CHECK(!Feed(gate, -80.0f, 0.0f));
  CHECK(!Feed(gate, -80.0f, 0.0f));
}

TEST(gate_opens_on_two_loud_frames_after_20ms) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  CHECK(!Feed(gate, -20.0f, 0.05f));
  CHECK(Feed(gate, -20.0f, 0.05f));
}

TEST(gate_ignores_quiet_noise_just_over_threshold_without_speech) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  for (int i = 0; i < 20; ++i) CHECK(!Feed(gate, -40.0f, 0.1f));
}

TEST(gate_opens_at_once_on_speech) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  CHECK(Feed(gate, -40.0f, 0.9f));
}

TEST(gate_speech_needs_to_clear_the_threshold) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  CHECK(!Feed(gate, -70.0f, 0.95f));
}

TEST(gate_closes_sooner_when_speech_probability_drops) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  for (int i = 0; i < 10; ++i) Feed(gate, -30.0f, 0.95f);
  const int with_vad = FramesUntilClosed(gate, -80.0f, 0.02f);

  gate.Initialize(16000, 1);
  for (int i = 0; i < 10; ++i) Feed(gate, -30.0f, -1.0f);
  const int without_vad = FramesUntilClosed(gate, -80.0f, -1.0f);
  std::printf("  frames to close: %d with speech probability, %d without\n",
              with_vad, without_vad);
  CHECK(with_vad <= 11);
  CHECK(without_vad == 31);
}

TEST(gate_keeps_the_full_hold_while_speech_stays_likely) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  for (int i = 0; i < 10; ++i) Feed(gate, -30.0f, 0.95f);
  CHECK(FramesUntilClosed(gate, -80.0f, 0.5f) == 31);
}

TEST(gate_without_speech_probability_matches_the_plain_level_gate) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  CHECK(Feed(gate, -40.0f, -1.0f));
  CHECK(Feed(gate, -40.0f, -1.0f));
  CHECK(FramesUntilClosed(gate, -80.0f, -1.0f) == 31);
}

TEST(gate_closed_output_is_silence_once_ramped) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(16000, 1);
  gate.Configure(true, kThresholdDb);
  for (int i = 0; i < 40; ++i) Feed(gate, -80.0f, 0.0f);
  std::vector<float> f = Frame(-20.0f);
  gate.ProcessWithSpeech(kFrame, f.data(), 0.05f);
  double energy = 0.0;
  for (float v : f) energy += v * v;
  CHECK(energy == 0.0);
}
