#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "check.h"
#include "flutter_deep_filter.h"
#include "flutter_noise_suppressor.h"

using flutter_webrtc_plugin::DeepFilter;
using flutter_webrtc_plugin::NoiseSuppressor;

namespace {

constexpr int kFrame = 480;

std::vector<float> Frame() {
  std::vector<float> x(kFrame);
  for (int i = 0; i < kFrame; ++i) x[i] = 300.0f * ((i * 37) % 19 - 9) / 9.0f;
  return x;
}

}  // namespace

TEST(strongest_loads_again_each_time_it_is_switched_on) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  NoiseSuppressor suppressor;
  std::vector<float> frame = Frame();
  for (int round = 0; round < 4; ++round) {
    suppressor.ConfigureStrongest(true);
    suppressor.WaitForStrongestModel();
    suppressor.Process(frame.data(), kFrame);
    CHECK(suppressor.StrongestActive());
    suppressor.ConfigureStrongest(false);
    suppressor.Process(frame.data(), kFrame);
    CHECK(!suppressor.StrongestActive());
  }
}

TEST(strongest_switched_off_and_on_between_two_frames_still_runs) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  NoiseSuppressor suppressor;
  std::vector<float> frame = Frame();
  suppressor.ConfigureStrongest(true);
  suppressor.WaitForStrongestModel();
  suppressor.Process(frame.data(), kFrame);
  suppressor.ConfigureStrongest(false);
  suppressor.ConfigureStrongest(true);
  suppressor.Process(frame.data(), kFrame);
  suppressor.WaitForStrongestModel();
  suppressor.Process(frame.data(), kFrame);
  CHECK(suppressor.StrongestActive());
}

TEST(strongest_survives_being_switched_while_frames_run) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  NoiseSuppressor suppressor;
  std::atomic<bool> stop{false};
  std::thread audio([&] {
    std::vector<float> frame = Frame();
    while (!stop.load()) {
      suppressor.Process(frame.data(), kFrame);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  for (int round = 0; round < 8; ++round) {
    suppressor.ConfigureStrongest(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20 + 15 * round));
    suppressor.ConfigureStrongest(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  suppressor.ConfigureStrongest(true);
  suppressor.WaitForStrongestModel();
  stop = true;
  audio.join();

  std::vector<float> frame = Frame();
  suppressor.Process(frame.data(), kFrame);
  CHECK(suppressor.StrongestActive());
}
