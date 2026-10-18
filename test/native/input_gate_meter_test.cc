#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <vector>

#include "check.h"
#include "flutter_input_gate.h"

namespace {

using flutter_webrtc_plugin::InputGate;

constexpr int kFrame = 480;
constexpr float kThresholdDb = -50.0f;

std::vector<float> Tone(float db) {
  std::vector<float> f(kFrame);
  const float amplitude = 32768.0f * std::pow(10.0f, db / 20.0f) * 1.41421356f;
  for (int i = 0; i < kFrame; ++i) {
    f[i] = amplitude *
           static_cast<float>(std::sin(2.0 * M_PI * 200.0 * i / 48000.0));
  }
  return f;
}

void Feed(InputGate& gate, float db) {
  std::vector<float> f = Tone(db);
  gate.ProcessWithSpeech(kFrame, f.data(), -1.0f);
}

}  // namespace

TEST(meter_reports_the_loudest_frame_since_the_last_read) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(48000, 1);
  gate.Configure(false, kThresholdDb);
  gate.TakePeakDb();
  Feed(gate, -70.0f);
  Feed(gate, -30.0f);
  Feed(gate, -70.0f);
  CHECK(std::fabs(gate.TakePeakDb() - -30.0f) < 0.5f);
}

TEST(meter_does_not_linger_over_the_threshold_after_a_click) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(48000, 1);
  gate.Configure(false, kThresholdDb);
  gate.TakePeakDb();
  Feed(gate, -20.0f);
  CHECK(gate.TakePeakDb() > kThresholdDb);
  // 12 frames is inside the old decay's reach: 1.5 dB each keeps -20 above -50.
  for (int i = 0; i < 12; ++i) Feed(gate, -80.0f);
  CHECK(gate.TakePeakDb() < kThresholdDb);
}

TEST(meter_forgets_a_loud_frame_when_a_new_session_starts) {
  InputGate& gate = *InputGate::Instance();
  gate.Initialize(48000, 1);
  Feed(gate, -20.0f);
  gate.Initialize(48000, 1);
  CHECK(gate.TakePeakDb() <= -100.0f);
  Feed(gate, -20.0f);
  gate.Reset(48000);
  CHECK(gate.TakePeakDb() <= -100.0f);
}

TEST(meter_is_floored_when_nothing_was_processed) {
  InputGate& gate = *InputGate::Instance();
  gate.TakePeakDb();
  CHECK(gate.TakePeakDb() <= -100.0f);
}
