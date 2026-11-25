#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "check.h"
#include "flutter_deep_filter.h"
#include "flutter_mic_sample_tap.h"
#include "flutter_noise_suppressor.h"

extern thread_local long g_thread_allocations;

namespace {

using flutter_webrtc_plugin::DeepFilter;
using flutter_webrtc_plugin::MicSampleTap;
using flutter_webrtc_plugin::NoiseSuppressor;
using namespace std::chrono_literals;

constexpr int kFrame = 480;

// A model that copies its input after a set delay, and fails on request.
class FakeModel : public DeepFilter {
 public:
  FakeModel(std::chrono::microseconds delay, bool fail)
      : delay_(delay), fail_(fail) {}

  bool Process(const float* in, float* out) override {
    if (delay_.count() > 0) std::this_thread::sleep_for(delay_);
    if (fail_) return false;
    for (int i = 0; i < kFrame; ++i) out[i] = in[i];
    return true;
  }

 private:
  std::chrono::microseconds delay_;
  bool fail_;
};

struct Loads {
  std::atomic<int> count{0};
};

// The first `bad` loads give a model that is slow or fails; later ones are healthy.
NoiseSuppressor::StrongestTuning Tuned(Loads& loads, int bad, std::chrono::microseconds delay,
                                       bool fail) {
  NoiseSuppressor::StrongestTuning tuning;
  tuning.budget_us = 2000;
  tuning.overrun_frames = 3;
  tuning.retry_ms = 30;
  tuning.overrun_retry_ms = 30;
  tuning.loader = [&loads, bad, delay, fail](const DeepFilter::Params&) {
    const int n = ++loads.count;
    return std::unique_ptr<DeepFilter>(
        n <= bad ? new FakeModel(delay, fail) : new FakeModel(0us, false));
  };
  return tuning;
}

std::vector<float> Frame() {
  std::vector<float> x(kFrame);
  for (int i = 0; i < kFrame; ++i) x[i] = 300.0f * ((i * 37) % 19 - 9) / 9.0f;
  return x;
}

void Frames(NoiseSuppressor& s, int n) {
  static std::vector<float> frame = Frame();
  for (int i = 0; i < n; ++i) s.Process(frame.data(), kFrame);
}

// Runs frames until Strongest is back, or three seconds pass.
bool RecoversWithin3s(NoiseSuppressor& s) {
  const auto until = std::chrono::steady_clock::now() + 3s;
  while (std::chrono::steady_clock::now() < until) {
    Frames(s, 1);
    if (s.StrongestActive()) return true;
    std::this_thread::sleep_for(5ms);
  }
  return false;
}

NoiseSuppressor& Started(NoiseSuppressor& s, const NoiseSuppressor::StrongestTuning& tuning) {
  s.SetStrongestTuning(tuning);
  s.ConfigureStrongest(true);
  s.WaitForStrongestModel();
  Frames(s, 1);
  return s;
}

}  // namespace

TEST(strongest_that_overruns_its_budget_in_a_row_hands_over_to_rnnoise) {
  Loads loads;
  NoiseSuppressor s;
  Started(s, Tuned(loads, 1, 5ms, false));
  CHECK(s.StrongestActive());
  Frames(s, 6);
  CHECK(!s.StrongestActive());
  CHECK(s.Active());
}

TEST(strongest_that_stays_inside_its_budget_keeps_running) {
  Loads loads;
  NoiseSuppressor s;
  Started(s, Tuned(loads, 0, 0us, false));
  Frames(s, 50);
  CHECK(s.StrongestActive());
  CHECK(loads.count == 1);
}

TEST(strongest_one_slow_frame_between_fast_ones_is_not_an_overrun) {
  NoiseSuppressor s;
  std::atomic<int> calls{0};
  struct Spiky : DeepFilter {
    explicit Spiky(std::atomic<int>& c) : calls(c) {}
    bool Process(const float* in, float* out) override {
      if (++calls % 2 == 0) std::this_thread::sleep_for(5ms);
      for (int i = 0; i < kFrame; ++i) out[i] = in[i];
      return true;
    }
    std::atomic<int>& calls;
  };
  NoiseSuppressor::StrongestTuning tuning;
  tuning.budget_us = 2000;
  tuning.overrun_frames = 3;
  tuning.loader = [&](const DeepFilter::Params&) {
    return std::unique_ptr<DeepFilter>(new Spiky(calls));
  };
  Started(s, tuning);
  Frames(s, 20);
  CHECK(s.StrongestActive());
}

TEST(strongest_that_failed_a_frame_loads_again_after_the_backoff) {
  Loads loads;
  NoiseSuppressor s;
  Started(s, Tuned(loads, 1, 0us, true));
  CHECK(!s.StrongestActive());
  CHECK(s.Active());
  CHECK(RecoversWithin3s(s));
  CHECK(loads.count == 2);
}

TEST(strongest_that_overran_loads_again_after_the_backoff) {
  Loads loads;
  NoiseSuppressor s;
  Started(s, Tuned(loads, 1, 5ms, false));
  Frames(s, 6);
  CHECK(!s.StrongestActive());
  CHECK(RecoversWithin3s(s));
}

TEST(strongest_that_failed_waits_while_the_backoff_runs) {
  Loads loads;
  NoiseSuppressor s;
  auto tuning = Tuned(loads, 1, 0us, true);
  tuning.retry_ms = 600000;
  Started(s, tuning);
  CHECK(!s.StrongestActive());
  std::this_thread::sleep_for(400ms);
  Frames(s, 3);
  CHECK(!s.StrongestActive());
  CHECK(loads.count == 1);
}

TEST(strongest_that_failed_loads_again_when_capture_restarts) {
  Loads loads;
  NoiseSuppressor s;
  auto tuning = Tuned(loads, 1, 0us, true);
  tuning.retry_ms = 600000;
  Started(s, tuning);
  CHECK(!s.StrongestActive());
  s.Restart();
  CHECK(RecoversWithin3s(s));
}

TEST(strongest_load_that_failed_is_tried_again_after_the_backoff) {
  NoiseSuppressor s;
  std::atomic<int> loads{0};
  NoiseSuppressor::StrongestTuning tuning;
  tuning.retry_ms = 30;
  tuning.loader = [&](const DeepFilter::Params&) {
    return ++loads == 1 ? nullptr : std::unique_ptr<DeepFilter>(new FakeModel(0us, false));
  };
  Started(s, tuning);
  CHECK(!s.StrongestActive());
  CHECK(RecoversWithin3s(s));
}

TEST(strongest_retiring_the_model_does_not_allocate_on_the_audio_thread) {
  Loads loads;
  NoiseSuppressor s;
  Started(s, Tuned(loads, 1, 0us, true));
  CHECK(RecoversWithin3s(s));
  Frames(s, 2);
  s.ConfigureStrongest(false);
  const long before = g_thread_allocations;
  Frames(s, 3);
  CHECK(g_thread_allocations == before);
  CHECK(!s.StrongestActive());
}

TEST(tap_converts_without_allocating_when_the_rate_was_known_at_start) {
  MicSampleTap tap;
  tap.SetCaptureRate(44100);
  tap.Start(5);
  std::vector<float> frame(441, 1000.0f);
  const long before = g_thread_allocations;
  for (int i = 0; i < 20; ++i) tap.Feed(frame.data(), 441);
  CHECK(g_thread_allocations == before);
  CHECK(tap.Take().size() == 20 * 480);
  CHECK(tap.SkippedFrames() == 0);
}

TEST(tap_never_waits_on_start_or_take_and_loses_no_sample_it_did_not_count_as_dropped) {
  MicSampleTap tap;
  tap.Start(120);
  std::atomic<bool> stop{false};
  std::atomic<size_t> taken{0};
  std::thread reader([&] {
    while (!stop.load()) taken += tap.Take().size();
  });
  std::vector<float> frame(kFrame, 500.0f);
  constexpr int kFed = 10000;
  for (int i = 0; i < kFed; ++i) tap.Feed(frame.data(), kFrame);
  stop = true;
  reader.join();
  const size_t total = taken.load() + tap.Take().size();
  CHECK(total + static_cast<size_t>(tap.DroppedFrames()) * kFrame ==
        static_cast<size_t>(kFed) * kFrame);
}
