#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <cmath>
#include <vector>

#include "check.h"
#include "flutter_capture_chain_api.h"

namespace {

constexpr int kFrames = 480;

// Runs n frames of a tone at `amplitude` (int16 scale) through the C entry
// points; returns the loudest output sample of the last frame.
float RunTone(int n, float amplitude) {
  float* frame = slimm_chain_frame_buffer();
  float peak = 0.0f;
  for (int k = 0; k < n; ++k) {
    for (int i = 0; i < kFrames; ++i) {
      frame[i] = amplitude *
                 std::sin(2.0f * static_cast<float>(M_PI) * 220.0f *
                          (k * kFrames + i) / 48000.0f);
    }
    slimm_chain_process(frame, kFrames);
    peak = 0.0f;
    for (int i = 0; i < kFrames; ++i) peak = std::max(peak, std::fabs(frame[i]));
  }
  return peak;
}

}  // namespace

TEST(chain_api_gates_quiet_frames_and_reports_the_level) {
  slimm_chain_configure(1, -50.0f, 0, 0, 0);
  slimm_chain_initialize(48000, 1);
  // About -70 dBFS: under the threshold, so it leaves as silence.
  CHECK(RunTone(80, 10.0f) == 0.0f);
  CHECK(slimm_chain_voice() == 0);
  const float quiet = slimm_chain_take_peak_db();
  CHECK(quiet < -60.0f && quiet > -80.0f);
  // About -20 dBFS opens the gate and passes through.
  CHECK(RunTone(20, 3000.0f) > 2000.0f);
  CHECK(slimm_chain_voice() == 1);
  const float loud = slimm_chain_take_peak_db();
  CHECK(loud > -25.0f && loud < -15.0f);
  // The peak is taken: a second read without new frames is the floor.
  CHECK(slimm_chain_take_peak_db() <= -99.0f);
}

TEST(chain_api_passes_everything_with_the_gate_off) {
  slimm_chain_configure(0, -50.0f, 0, 0, 0);
  slimm_chain_initialize(48000, 1);
  CHECK(RunTone(20, 10.0f) > 5.0f);
  CHECK(slimm_chain_strong_active() == 0);
}

TEST(chain_api_runs_rnnoise_when_strong_is_on) {
  slimm_chain_configure(0, -50.0f, 1, 1, 1);
  slimm_chain_initialize(48000, 1);
  RunTone(5, 3000.0f);
  CHECK(slimm_chain_strong_active() == 1);
  CHECK(slimm_chain_dynamics() == 1);
  slimm_chain_configure(0, -50.0f, 0, 0, 0);
  RunTone(1, 3000.0f);
  CHECK(slimm_chain_strong_active() == 0);
  CHECK(slimm_chain_dynamics() == 0);
  CHECK(slimm_chain_leveler_gain_db() >= 0.0f);
}

TEST(chain_api_reports_a_frame_it_cannot_process) {
  slimm_chain_configure(1, -50.0f, 0, 0, 0);
  slimm_chain_initialize(48000, 1);
  const int too_long = slimm_chain_max_frame_samples() + 1;
  std::vector<float> frame(too_long, 1000.0f);
  CHECK(slimm_chain_process(frame.data(), too_long) == 0);
  CHECK(frame[0] == 1000.0f);
  CHECK(slimm_chain_process(slimm_chain_frame_buffer(), kFrames) == 1);
}
