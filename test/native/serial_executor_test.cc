#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "check.h"
#include "serial_executor.h"

namespace {

using flutter_webrtc_plugin::SerialExecutor;
using namespace std::chrono_literals;

TEST(SerialExecutor_RunsTasksInOrderOffTheCallingThread) {
  std::vector<int> order;
  std::atomic<bool> off_thread{true};
  std::atomic<int> done{0};
  {
    SerialExecutor executor;
    const auto caller = std::this_thread::get_id();
    for (int i = 0; i < 5; ++i) {
      executor.Post([&, i] {
        off_thread = off_thread && std::this_thread::get_id() != caller;
        order.push_back(i);
        ++done;
      });
    }
    while (done < 5) {
      std::this_thread::sleep_for(1ms);
    }
  }
  CHECK((order == std::vector<int>{0, 1, 2, 3, 4}));
  CHECK(off_thread.load());
}

TEST(SerialExecutor_StopWaitsForTheRunningTaskAndDropsTheQueued) {
  std::atomic<bool> started{false};
  std::atomic<bool> first_finished{false};
  std::atomic<bool> queued_ran{false};
  {
    SerialExecutor executor;
    executor.Post([&] {
      started = true;
      std::this_thread::sleep_for(150ms);
      first_finished = true;
    });
    executor.Post([&] { queued_ran = true; });
    while (!started) {
      std::this_thread::sleep_for(1ms);
    }
  }
  CHECK(first_finished.load());
  CHECK(!queued_ran.load());
}

TEST(SerialExecutor_RefusesTasksAfterStop) {
  SerialExecutor executor;
  std::atomic<bool> ran{false};
  executor.Stop();
  CHECK(!executor.Post([&] { ran = true; }));
  std::this_thread::sleep_for(20ms);
  CHECK(!ran.load());
}

TEST(SerialExecutor_TaskCanPostAnother) {
  std::atomic<int> ran{0};
  {
    SerialExecutor executor;
    executor.Post([&] {
      ++ran;
      executor.Post([&] { ++ran; });
    });
    while (ran < 2) {
      std::this_thread::sleep_for(1ms);
    }
  }
  CHECK(ran.load() == 2);
}

TEST(SerialExecutor_NeverStartedStopsCleanly) {
  SerialExecutor executor;
  executor.Stop();
  executor.Stop();
}

}  // namespace
