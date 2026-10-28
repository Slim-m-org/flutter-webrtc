#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <random>
#include <vector>

#include "check.h"
#include "flutter_noise_suppressor.h"
#include "flutter_onset_cover.h"

using flutter_webrtc_plugin::DeepFilter;
using flutter_webrtc_plugin::NoiseSuppressor;
using flutter_webrtc_plugin::OnsetCover;

namespace {

constexpr int kFrame = 480;

double Rms(const std::vector<float>& x, size_t from, size_t to) {
  double sum = 0.0;
  for (size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
  return std::sqrt(sum / static_cast<double>(to - from));
}

std::vector<float> WhiteNoise(size_t n, float amplitude, unsigned seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<float> dist(0.0f, amplitude);
  std::vector<float> x(n);
  for (float& v : x) v = dist(rng);
  return x;
}

void Run(NoiseSuppressor& suppressor, std::vector<float>& x) {
  for (size_t at = 0; at + kFrame <= x.size(); at += kFrame) {
    suppressor.Process(&x[at], kFrame);
  }
}

// Feeds `frames` frames of one probability and returns the last weight.
float Feed(OnsetCover& cover, const OnsetCover::Tuning& tuning, float p,
           int frames) {
  float weight = 0.0f;
  for (int i = 0; i < frames; ++i) weight = cover.Step(p, tuning);
  return weight;
}

}  // namespace

TEST(onset_cover_takes_over_when_speech_follows_a_quiet_spell) {
  OnsetCover cover;
  const OnsetCover::Tuning tuning;
  Feed(cover, tuning, 0.0f, 100);
  CHECK(Feed(cover, tuning, 0.9f, tuning.confirm_frames) == 1.0f);
}

TEST(onset_cover_ignores_a_click_shorter_than_a_word) {
  OnsetCover cover;
  const OnsetCover::Tuning tuning;
  Feed(cover, tuning, 0.0f, 100);
  float peak = 0.0f;
  for (int click = 0; click < 5; ++click) {
    peak = std::max(peak, Feed(cover, tuning, 0.9f, tuning.confirm_frames - 1));
    peak = std::max(peak, Feed(cover, tuning, 0.0f, 3));
  }
  CHECK(peak == 0.0f);
}

TEST(onset_cover_keeps_part_of_rnnoise_while_the_word_goes_on) {
  OnsetCover cover;
  const OnsetCover::Tuning tuning;
  Feed(cover, tuning, 0.0f, 100);
  Feed(cover, tuning, 0.9f, tuning.confirm_frames + tuning.cover_frames);
  CHECK(Feed(cover, tuning, 0.9f, 50) == tuning.speaking_weight);
  CHECK(Feed(cover, tuning, 0.0f, tuning.release_frames + 1) == 0.0f);
}

TEST(onset_cover_does_not_cover_speech_that_follows_speech) {
  OnsetCover cover;
  const OnsetCover::Tuning tuning;
  Feed(cover, tuning, 0.0f, 100);
  Feed(cover, tuning, 0.9f, 40);
  Feed(cover, tuning, 0.0f, tuning.quiet_frames - 5);
  Feed(cover, tuning, 0.9f, tuning.confirm_frames);
  CHECK(Feed(cover, tuning, 0.9f, 1) < 1.0f);
}

TEST(onset_cover_waits_out_the_first_frames_of_a_capture) {
  OnsetCover cover;
  const OnsetCover::Tuning tuning;
  CHECK(Feed(cover, tuning, 0.9f, 40) == 0.0f);
}

TEST(strongest_runs_deep_filter_and_removes_more_than_strong) {
  if (!DeepFilter::Compiled()) return;
  const std::vector<float> noise = WhiteNoise(kFrame * 300, 300.0f, 5);
  NoiseSuppressor strong;
  strong.Configure(true);
  std::vector<float> a = noise;
  Run(strong, a);
  CHECK(!strong.StrongestActive());

  NoiseSuppressor strongest;
  strongest.ConfigureStrongest(true);
  std::vector<float> b = noise;
  Run(strongest, b);
  CHECK(strongest.Active());
  CHECK(strongest.StrongestActive());
  const size_t from = noise.size() / 2;
  CHECK(Rms(b, from, b.size()) < Rms(a, from, a.size()));
}

TEST(strongest_without_the_model_is_exactly_strong) {
  const std::vector<float> noise = WhiteNoise(kFrame * 100, 300.0f, 9);
  NoiseSuppressor strong;
  strong.Configure(true);
  std::vector<float> a = noise;
  Run(strong, a);

  NoiseSuppressor fallback;
  NoiseSuppressor::StrongestTuning tuning;
  tuning.allow_model = false;
  fallback.SetStrongestTuning(tuning);
  fallback.ConfigureStrongest(true);
  std::vector<float> b = noise;
  Run(fallback, b);
  CHECK(fallback.Active());
  CHECK(!fallback.StrongestActive());
  CHECK(a == b);
}

TEST(strongest_follows_the_switch_and_keeps_a_stereo_second_channel) {
  NoiseSuppressor suppressor;
  suppressor.ConfigureStrongest(true);
  CHECK(suppressor.StrongestEnabled());
  std::vector<float> left = WhiteNoise(kFrame, 300.0f, 1);
  std::vector<float> right = WhiteNoise(kFrame, 300.0f, 2);
  float* planes[2] = {left.data(), right.data()};
  for (int i = 0; i < 60; ++i) suppressor.ProcessPlanar(planes, 2, kFrame);
  CHECK(suppressor.Active());
  suppressor.ConfigureStrongest(false);
  suppressor.Configure(false);
  suppressor.Process(left.data(), kFrame);
  CHECK(!suppressor.StrongestActive());
  CHECK(!suppressor.Active());
}
