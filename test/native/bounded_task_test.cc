#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "bounded_task.h"
#include "check.h"

namespace {

using flutter_webrtc_plugin::RunBounded;
using std::chrono::milliseconds;

TEST(BoundedTask_ReturnsTheResultOfAFastTask) {
  auto got = RunBounded<int>(milliseconds(2000), [] { return 7; }, [](int&) {});
  CHECK(got.has_value());
  CHECK(*got == 7);
}

TEST(BoundedTask_GivesUpOnASlowTaskWithoutWaitingForIt) {
  const auto start = std::chrono::steady_clock::now();
  auto got = RunBounded<int>(
      milliseconds(50),
      [] {
        std::this_thread::sleep_for(milliseconds(600));
        return 1;
      },
      [](int&) {});
  const auto waited = std::chrono::steady_clock::now() - start;
  CHECK(!got.has_value());
  CHECK(waited < milliseconds(400));
}

TEST(BoundedTask_DiscardsTheResultOfATaskThatFinishesTooLate) {
  auto discarded = std::make_shared<std::atomic<int>>(0);
  auto got = RunBounded<int>(
      milliseconds(20),
      [] {
        std::this_thread::sleep_for(milliseconds(150));
        return 5;
      },
      [discarded](int& v) { *discarded += v; });
  CHECK(!got.has_value());
  for (int i = 0; i < 100 && *discarded == 0; ++i) {
    std::this_thread::sleep_for(milliseconds(20));
  }
  CHECK(*discarded == 5);
}

TEST(BoundedTask_NeverDiscardsAResultItReturned) {
  auto discarded = std::make_shared<std::atomic<int>>(0);
  auto got = RunBounded<int>(
      milliseconds(2000), [] { return 3; },
      [discarded](int&) { ++*discarded; });
  std::this_thread::sleep_for(milliseconds(50));
  CHECK(got.has_value());
  CHECK(*discarded == 0);
}

}  // namespace
