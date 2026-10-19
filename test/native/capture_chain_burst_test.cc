#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
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

// Anything above this in a stretch that should be silent is audible.
constexpr float kAudible = 2.0f;

struct Stream {
  int rate;
  unsigned seed = 7;

  int frames() const { return rate / 100; }

  // Runs n 10 ms frames of the generator through the chain; returns the
  // loudest output sample seen.
  template <typename Gen>
  float Run(int n, Gen gen) {
    float peak = 0.0f;
    std::vector<float> f(frames());
    for (int k = 0; k < n; ++k) {
      for (int i = 0; i < frames(); ++i) f[i] = gen(i);
      CaptureChain::Instance()->Process(1, frames(), frames(), f.data());
      for (float v : f) {
        peak = std::isfinite(v) ? std::max(peak, std::fabs(v))
                                : std::numeric_limits<float>::infinity();
      }
    }
    return peak;
  }

  float Speech(int i) {
    seed = seed * 1664525u + 1013904223u;
    const float u = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
    const float tone = std::sin(2.0f * static_cast<float>(M_PI) * 180.0f * i / rate);
    return 6000.0f * tone + 1500.0f * u;
  }
};

void Configure(bool strong, bool gate, bool dynamics) {
  NoiseSuppressor::Instance()->Configure(strong);
  InputGate::Instance()->Configure(gate, -50.0f);
  VoiceDynamics::Instance()->Configure(dynamics);
  CaptureChain::Instance()->Initialize(48000, 1);
}

void SpeechThenSilence(Stream& s, int silent_frames) {
  s.Run(300, [&](int i) { return s.Speech(i); });
}

float SilenceAfterSpeech(int from_rate, int to_rate) {
  Configure(true, true, true);
  Stream a{from_rate};
  SpeechThenSilence(a, 0);
  a.Run(200, [](int) { return 0.0f; });
  Stream b{to_rate};
  return b.Run(300, [](int) { return 0.0f; });
}

}  // namespace

TEST(chain_is_silent_after_a_rate_switch_in_digital_silence) {
  const int rates[] = {48000, 44100, 16000, 32000, 24000};
  for (int from : rates) {
    for (int to : rates) {
      const float peak = SilenceAfterSpeech(from, to);
      if (peak > kAudible) {
        std::printf("  %d -> %d Hz: burst of %.0f in silence\n", from, to, peak);
      }
      CHECK(peak <= kAudible);
    }
  }
}

TEST(chain_is_silent_after_a_restart_mid_speech) {
  Configure(true, true, true);
  Stream s{48000};
  s.Run(100, [&](int i) { return s.Speech(i); });
  CaptureChain::Instance()->Reset(48000);
  CaptureChain::Instance()->Initialize(48000, 1);
  CHECK(s.Run(300, [](int) { return 0.0f; }) <= kAudible);
}

TEST(chain_is_silent_after_a_restart_at_another_rate) {
  Configure(true, true, true);
  Stream s{48000};
  s.Run(100, [&](int i) { return s.Speech(i); });
  CaptureChain::Instance()->Reset(44100);
  Stream t{44100};
  CHECK(t.Run(300, [](int) { return 0.0f; }) <= kAudible);
}

TEST(chain_recovers_from_a_non_finite_frame) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  for (bool strong : {false, true}) {
    Configure(strong, true, true);
    Stream s{48000};
    s.Run(100, [&](int i) { return s.Speech(i); });
    s.Run(1, [&](int i) { return i == 100 ? nan : s.Speech(i); });
    s.Run(150, [](int) { return 0.0f; });
    const float peak = s.Run(100, [](int) { return 0.0f; });
    std::printf("  strong=%d: peak %.1f after a NaN sample\n", strong, peak);
    CHECK(std::isfinite(peak) && peak <= kAudible);
    s.Run(100, [&](int i) { return s.Speech(i); });
    CHECK(std::isfinite(s.Run(10, [&](int i) { return s.Speech(i); })));
  }
}

TEST(chain_does_not_make_a_tone_from_a_dc_offset) {
  Configure(false, false, true);
  Stream s{48000};
  s.Run(200, [](int) { return 3000.0f; });
  s.Run(5, [](int) { return 0.0f; });
  CHECK(s.Run(100, [](int) { return 0.0f; }) <= kAudible);
}

TEST(chain_dynamics_flushes_denormals_after_loud_speech) {
  Configure(false, true, true);
  Stream s{48000};
  s.Run(300, [&](int i) { return s.Speech(i); });
  using Clock = std::chrono::steady_clock;
  double worst_us = 0.0;
  for (int k = 0; k < 6000; ++k) {
    const auto start = Clock::now();
    s.Run(1, [](int) { return 0.0f; });
    const double us =
        std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    worst_us = std::max(worst_us, us);
  }
  std::printf("  slowest silent frame after speech: %.1f us\n", worst_us);
  CHECK(worst_us < 2000.0);
}

TEST(chain_adds_no_burst_to_room_noise_across_rate_switches) {
  const int rates[] = {48000, 44100, 16000, 48000, 32000, 44100, 24000, 48000};
  Configure(true, false, true);
  Stream s{48000};
  float worst_ratio = 0.0f;
  for (int rate : rates) {
    s.rate = rate;
    // Room noise at about -60 dBFS RMS; peaks stay under 100.
    auto noise = [&](int) {
      s.seed = s.seed * 1664525u + 1013904223u;
      return (static_cast<float>(s.seed >> 8) / 8388608.0f - 1.0f) * 55.0f;
    };
    const float peak = s.Run(150, noise);
    worst_ratio = std::max(worst_ratio, peak / 55.0f);
  }
  std::printf("  output peak over input peak across switches: %.2f\n",
              worst_ratio);
  CHECK(worst_ratio < 1.5f);
}
