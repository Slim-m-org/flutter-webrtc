#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <cstdint>
#include <vector>

#include "check.h"
#include "flutter_capture_chain.h"
#include "flutter_input_gate.h"
#include "flutter_mic_sample_tap.h"
#include "flutter_noise_suppressor.h"
#include "flutter_voice_dynamics.h"

extern thread_local long g_thread_allocations;

namespace {

using flutter_webrtc_plugin::CaptureChain;
using flutter_webrtc_plugin::InputGate;
using flutter_webrtc_plugin::MicSampleTap;
using flutter_webrtc_plugin::NoiseSuppressor;
using flutter_webrtc_plugin::VoiceDynamics;

std::vector<float> Tone(int rate_hz, float amplitude, int frame_index) {
  const int n = rate_hz / 100;
  std::vector<float> f(n);
  for (int i = 0; i < n; ++i) {
    const double t = static_cast<double>(frame_index * n + i) / rate_hz;
    f[i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * t));
  }
  return f;
}

}  // namespace

TEST(tap_keeps_nothing_until_started) {
  MicSampleTap tap;
  std::vector<float> f = Tone(48000, 1000.0f, 0);
  tap.Feed(f.data(), static_cast<int>(f.size()));
  CHECK(tap.Take().empty());
}

TEST(tap_keeps_each_frame_as_16_bit_samples_at_48k) {
  MicSampleTap tap;
  tap.Start(30);
  for (int k = 0; k < 3; ++k) {
    std::vector<float> f = Tone(48000, 12000.0f, k);
    tap.Feed(f.data(), static_cast<int>(f.size()));
  }
  const std::vector<int16_t> got = tap.Take();
  CHECK(got.size() == 3 * 480);
  const std::vector<float> first = Tone(48000, 12000.0f, 0);
  for (int i = 0; i < 480; ++i) {
    CHECK(std::abs(got[i] - std::lrintf(first[i])) <= 1);
  }
  CHECK(tap.Take().empty());
}

TEST(tap_stops_at_the_limit_and_says_so) {
  MicSampleTap tap;
  tap.Start(1);
  for (int k = 0; k < 150; ++k) {
    std::vector<float> f = Tone(48000, 5000.0f, k);
    tap.Feed(f.data(), static_cast<int>(f.size()));
  }
  CHECK(tap.Full());
  CHECK(!tap.Armed());
  CHECK(tap.Take().size() == 48000);
}

TEST(tap_limit_counts_samples_already_taken) {
  MicSampleTap tap;
  tap.Start(1);
  size_t total = 0;
  for (int k = 0; k < 150; ++k) {
    std::vector<float> f = Tone(48000, 5000.0f, k);
    tap.Feed(f.data(), static_cast<int>(f.size()));
    if (k % 7 == 0) total += tap.Take().size();
  }
  total += tap.Take().size();
  CHECK(total == 48000);
}

TEST(tap_clips_instead_of_wrapping) {
  MicSampleTap tap;
  tap.Start(1);
  std::vector<float> f(480, 50000.0f);
  tap.Feed(f.data(), 480);
  const std::vector<int16_t> got = tap.Take();
  CHECK(got.size() == 480 && got[0] == 32767);
}

TEST(tap_brings_a_16k_capture_to_48k) {
  MicSampleTap tap;
  tap.Start(30);
  for (int k = 0; k < 20; ++k) {
    std::vector<float> f = Tone(16000, 10000.0f, k);
    tap.Feed(f.data(), static_cast<int>(f.size()));
  }
  CHECK(tap.Take().size() == 20 * 480);
  CHECK(tap.SkippedFrames() == 0);
}

TEST(tap_sees_the_frame_before_the_chain_changes_it) {
  NoiseSuppressor::Instance()->Configure(true);
  InputGate::Instance()->Configure(true, -10.0f);
  VoiceDynamics::Instance()->Configure(true);
  CaptureChain* chain = CaptureChain::Instance();
  chain->Initialize(48000, 1);
  MicSampleTap* tap = MicSampleTap::Instance();
  tap->Start(30);
  std::vector<std::vector<float>> sent;
  for (int k = 0; k < 40; ++k) {
    std::vector<float> f = Tone(48000, 40.0f, k);
    sent.push_back(f);
    chain->Process(1, 480, 480, f.data());
  }
  tap->Stop();
  const std::vector<int16_t> got = tap->Take();
  CHECK(got.size() == 40 * 480);
  int differing = 0;
  for (int k = 0; k < 40; ++k) {
    for (int i = 0; i < 480; ++i) {
      if (std::abs(got[k * 480 + i] - std::lrintf(sent[k][i])) > 1) ++differing;
    }
  }
  CHECK(differing == 0);
}

TEST(tap_feed_never_allocates_when_a_start_lands_between_the_halves_of_a_take) {
  MicSampleTap* tap = MicSampleTap::Instance();
  tap->SetCaptureRate(48000);
  tap->Start(1);
  tap->Take([&] { tap->Start(3); });
  const std::vector<float> frame = Tone(48000, 1000.0f, 0);
  const long before = g_thread_allocations;
  for (int i = 0; i < 250; ++i) tap->Feed(frame.data(), 480);
  CHECK(g_thread_allocations == before);
  tap->Stop();
}
