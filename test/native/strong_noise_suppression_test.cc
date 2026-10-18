#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

#include "check.h"
#include "flutter_noise_suppressor.h"
#include "flutter_rate_converter.h"

using flutter_webrtc_plugin::NoiseSuppressor;
using flutter_webrtc_plugin::RateConverter;

namespace {

long g_allocations = 0;

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

std::vector<float> Sine(size_t n, int rate, double hz, float amplitude) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    x[i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * i / rate));
  }
  return x;
}

}  // namespace

void* operator new(size_t size) {
  ++g_allocations;
  void* p = std::malloc(size);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

constexpr int kRates[] = {8000, 16000, 24000, 32000, 44100, 48000, 96000};

TEST(rate_converter_round_trip_keeps_level_and_adds_little_delay) {
  for (int rate : kRates) {
    if (rate == 48000) continue;
    RateConverter up, down;
    CHECK(up.Configure(rate, 48000));
    CHECK(down.Configure(48000, rate));
    const int frames = rate / 100;
    const int count = 100;
    const std::vector<float> in = Sine(frames * count, rate, 1000.0, 8000.0f);
    std::vector<float> out(in.size());
    std::vector<float> mid(480);
    for (int f = 0; f < count; ++f) {
      up.Convert(&in[f * frames], mid.data());
      down.Convert(mid.data(), &out[f * frames]);
    }
    const size_t from = in.size() / 2;
    const double ratio = Rms(out, from, in.size()) / Rms(in, from, in.size());

    // Delay: the lag that best matches noise through the pair to the noise.
    const std::vector<float> noise = WhiteNoise(frames * count, 1000.0f, 11);
    std::vector<float> noise_out(noise.size());
    up.Reset();
    down.Reset();
    for (int f = 0; f < count; ++f) {
      up.Convert(&noise[f * frames], mid.data());
      down.Convert(mid.data(), &noise_out[f * frames]);
    }
    int best_lag = 0;
    double best = -1e30;
    for (int lag = 0; lag < 100; ++lag) {
      double dot = 0.0;
      for (size_t i = from; i < in.size(); ++i) dot += noise_out[i] * noise[i - lag];
      if (dot > best) { best = dot; best_lag = lag; }
    }
    const double delay_ms = 1000.0 * best_lag / rate;
    std::printf("  %5d Hz: round trip gain %.4f, delay %d samples (%.2f ms)\n",
                rate, ratio, best_lag, delay_ms);
    CHECK(ratio > 0.97 && ratio < 1.03);
    CHECK(delay_ms < 2.5);
  }
}

TEST(strong_runs_at_every_rate_with_right_frame_count) {
  for (int rate : kRates) {
    NoiseSuppressor suppressor;
    suppressor.Configure(true);
    const int frames = rate / 100;
    std::vector<float> buffer = WhiteNoise(frames, 500.0f, 1);
    const float probability = suppressor.Process(buffer.data(), frames);
    CHECK(suppressor.Active());
    CHECK(probability >= 0.0f && probability <= 1.0f);
  }
}

TEST(strong_attenuates_steady_noise_at_every_rate) {
  for (int rate : kRates) {
    NoiseSuppressor suppressor;
    suppressor.Configure(true);
    const int frames = rate / 100;
    const int count = 200;
    std::vector<float> in = WhiteNoise(frames * count, 600.0f, 7);
    std::vector<float> out = in;
    for (int f = 0; f < count; ++f) {
      suppressor.Process(&out[f * frames], frames);
    }
    const size_t from = in.size() / 2;
    const double db = 20.0 * std::log10(Rms(out, from, in.size()) /
                                        Rms(in, from, in.size()));
    std::printf("  %5d Hz: white noise %.1f dB\n", rate, db);
    CHECK(suppressor.Active());
    CHECK(db < -10.0);
  }
}

TEST(strong_stereo_processes_both_channels) {
  for (int rate : kRates) {
    NoiseSuppressor suppressor;
    suppressor.Configure(true);
    const int frames = rate / 100;
    const int count = 200;
    std::vector<float> left = WhiteNoise(frames * count, 600.0f, 3);
    std::vector<float> right = WhiteNoise(frames * count, 600.0f, 4);
    const std::vector<float> left_in = left, right_in = right;
    for (int f = 0; f < count; ++f) {
      float* planes[2] = {&left[f * frames], &right[f * frames]};
      suppressor.ProcessPlanar(planes, 2, frames);
    }
    const size_t from = left.size() / 2;
    const double left_db = 20.0 * std::log10(Rms(left, from, left.size()) /
                                             Rms(left_in, from, left.size()));
    const double right_db = 20.0 * std::log10(Rms(right, from, right.size()) /
                                              Rms(right_in, from, right.size()));
    std::printf("  %5d Hz: stereo noise L %.1f dB, R %.1f dB\n", rate, left_db,
                right_db);
    CHECK(left_db < -10.0 && right_db < -10.0);
  }
}

TEST(strong_off_leaves_audio_alone_and_reports_inactive) {
  NoiseSuppressor suppressor;
  std::vector<float> buffer = WhiteNoise(160, 500.0f, 5);
  const std::vector<float> before = buffer;
  CHECK(suppressor.Process(buffer.data(), 160) < 0.0f);
  CHECK(!suppressor.Active());
  CHECK(buffer == before);
}

TEST(strong_does_not_allocate_after_the_first_frame) {
  for (int rate : kRates) {
    NoiseSuppressor suppressor;
    suppressor.Configure(true);
    const int frames = rate / 100;
    std::vector<float> buffer = WhiteNoise(frames, 500.0f, 9);
    suppressor.Process(buffer.data(), frames);
    const long before = g_allocations;
    for (int i = 0; i < 100; ++i) suppressor.Process(buffer.data(), frames);
    std::printf("  %5d Hz: %ld allocations over 100 frames\n", rate,
                g_allocations - before);
    CHECK(g_allocations == before);
  }
}
