#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <cmath>
#include <vector>

#include "check.h"
#include "flutter_voice_dynamics.h"

namespace {

using flutter_webrtc_plugin::VoiceDynamics;

constexpr int kRate = 48000;
constexpr int kFrame = kRate / 100;
constexpr float kFullScale = 32768.0f;

float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

float RmsDb(const std::vector<float>& f) {
  double sum = 0.0;
  for (float v : f) sum += static_cast<double>(v) * v;
  return 10.0f * std::log10(static_cast<float>(sum / f.size()) / (kFullScale * kFullScale) + 1e-12f);
}

// A 200 Hz tone at `level_db` RMS, which is what a vowel looks like to the leveler.
void Tone(std::vector<float>& f, int frame, float level_db) {
  const float amp = DbToLinear(level_db) * kFullScale * std::sqrt(2.0f);
  for (int i = 0; i < kFrame; ++i) {
    f[i] = amp * std::sin(2.0f * static_cast<float>(M_PI) * 200.0f * (frame * kFrame + i) / kRate);
  }
}

struct Run {
  float last_out_db = -200.0f;
  float peak = 0.0f;
};

// `n` frames of the tone at `level_db` with speech probability `p`.
Run Frames(int n, float level_db, float p) {
  Run run;
  std::vector<float> f(kFrame);
  for (int k = 0; k < n; ++k) {
    Tone(f, k, level_db);
    VoiceDynamics::Instance()->Process(f.data(), kFrame, p);
    run.last_out_db = RmsDb(f);
    for (float v : f) run.peak = std::max(run.peak, std::fabs(v));
  }
  return run;
}

void Fresh(bool leveler, bool dynamics) {
  VoiceDynamics::Instance()->Configure(dynamics);
  VoiceDynamics::Instance()->ConfigureLeveler(leveler);
  VoiceDynamics::Instance()->Restart();
}

float Gain() { return VoiceDynamics::Instance()->LevelerGainDb(); }

}  // namespace

TEST(leveler_lifts_quiet_speech_towards_the_target) {
  Fresh(true, false);
  const Run run = Frames(1500, -30.0f, 1.0f);
  CHECK(Gain() > 9.5f && Gain() <= 10.5f);
  CHECK(std::fabs(run.last_out_db - -20.0f) < 1.0f);
}

TEST(leveler_never_lifts_past_its_ceiling) {
  Fresh(true, false);
  const Run run = Frames(2000, -40.0f, 1.0f);
  CHECK(Gain() > 11.9f && Gain() <= 12.0f);
  CHECK(std::fabs(run.last_out_db - -28.0f) < 1.0f);
}

TEST(a_sound_in_a_pause_gets_the_voice_gain_and_no_more) {
  Fresh(true, false);
  Frames(1500, -30.0f, 1.0f);
  const float before = Gain();
  const Run gap = Frames(30, -30.0f, 0.05f);
  CHECK(std::fabs(Gain() - before) < 0.01f);
  CHECK(std::fabs(gap.last_out_db - (-30.0f + before)) < 0.3f);
}

TEST(silence_and_unsure_frames_leave_the_gain_alone) {
  Fresh(true, false);
  Frames(1500, -30.0f, 1.0f);
  const float before = Gain();
  Frames(500, -200.0f, 0.0f);
  CHECK(std::fabs(Gain() - before) < 0.01f);
  Frames(500, -30.0f, 0.5f);
  CHECK(std::fabs(Gain() - before) < 0.01f);
}

TEST(a_short_burst_the_suppressor_calls_speech_does_not_adapt) {
  Fresh(true, false);
  Frames(4, -30.0f, 1.0f);
  Frames(20, -200.0f, 0.0f);
  CHECK(Gain() == 0.0f);
}

TEST(loud_speech_brings_the_gain_down_fast_and_under_the_ceiling) {
  Fresh(true, false);
  Frames(1500, -30.0f, 1.0f);
  CHECK(Gain() > 9.0f);
  const Run loud = Frames(300, -6.0f, 1.0f);
  CHECK(Gain() < 0.5f);
  CHECK(loud.peak <= kFullScale * DbToLinear(-1.0f) + 1.0f);
}

TEST(the_leveler_rides_under_the_compressor) {
  Fresh(true, true);
  const Run run = Frames(1500, -30.0f, 1.0f);
  CHECK(Gain() > 9.5f);
  CHECK(std::fabs(run.last_out_db - -20.0f) < 1.0f);
}

TEST(leveler_off_and_dynamics_off_leave_samples_untouched) {
  Fresh(false, false);
  std::vector<float> f(kFrame), copy(kFrame);
  Tone(f, 0, -20.0f);
  copy = f;
  VoiceDynamics::Instance()->Process(f.data(), kFrame, 1.0f);
  CHECK(f == copy);
  CHECK(Gain() == 0.0f);
}

TEST(turning_the_leveler_off_drops_the_lift) {
  Fresh(true, false);
  Frames(1500, -30.0f, 1.0f);
  CHECK(Gain() > 9.0f);
  VoiceDynamics::Instance()->ConfigureLeveler(false);
  const Run run = Frames(2, -30.0f, 1.0f);
  CHECK(Gain() == 0.0f);
  CHECK(std::fabs(run.last_out_db - -30.0f) < 0.3f);
}
