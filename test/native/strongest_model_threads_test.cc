// Linux only: the linker wraps the model's create and free, so a test sees
// which thread runs them and can hold them. See CMakeLists.txt.
#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

#include "check.h"
#include "flutter_deep_filter.h"
#include "flutter_noise_suppressor.h"

using flutter_webrtc_plugin::DeepFilter;
using flutter_webrtc_plugin::NoiseSuppressor;

struct Dfn;

extern "C" {
Dfn* __real_slimm_dfn_create(float, float, float, float);
void __real_slimm_dfn_free(Dfn*);
}

namespace {

constexpr int kFrame = 480;

std::atomic<bool> g_hold_create{false};
std::atomic<bool> g_hold_free{false};
std::atomic<int> g_creates_started{0};
std::atomic<int> g_creates_done{0};
std::atomic<int> g_frees_started{0};
std::atomic<int> g_frees_done{0};
std::atomic<size_t> g_create_thread{0};
std::atomic<size_t> g_free_thread{0};

size_t Self() { return std::hash<std::thread::id>()(std::this_thread::get_id()); }

// A held call gives up after five seconds, so a mistake fails the test
// instead of hanging it.
void WaitWhile(const std::atomic<bool>& held) {
  for (int i = 0; i < 5000 && held.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void WaitUntil(const std::function<bool()>& done) {
  for (int i = 0; i < 5000 && !done(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void ResetHooks() {
  g_hold_create = false;
  g_hold_free = false;
  g_creates_started = 0;
  g_creates_done = 0;
  g_frees_started = 0;
  g_frees_done = 0;
}

std::vector<float> Frame() {
  std::vector<float> x(kFrame);
  for (int i = 0; i < kFrame; ++i) x[i] = 300.0f * ((i * 37) % 19 - 9) / 9.0f;
  return x;
}

// Strongest on, and the audio thread has taken the model up.
void TakeTheModel(NoiseSuppressor& suppressor, std::vector<float>& frame) {
  suppressor.ConfigureStrongest(true);
  suppressor.WaitForStrongestModel();
  suppressor.Process(frame.data(), kFrame);
}

}  // namespace

extern "C" {

Dfn* __wrap_slimm_dfn_create(float a, float b, float c, float d) {
  g_create_thread = Self();
  ++g_creates_started;
  WaitWhile(g_hold_create);
  Dfn* dfn = __real_slimm_dfn_create(a, b, c, d);
  ++g_creates_done;
  return dfn;
}

void __wrap_slimm_dfn_free(Dfn* dfn) {
  g_free_thread = Self();
  ++g_frees_started;
  WaitWhile(g_hold_free);
  __real_slimm_dfn_free(dfn);
  ++g_frees_done;
}

}  // extern "C"

TEST(strongest_model_is_loaded_off_the_thread_that_chose_it) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  ResetHooks();
  g_hold_create = true;
  NoiseSuppressor suppressor;
  std::vector<float> frame = Frame();
  suppressor.ConfigureStrongest(true);
  CHECK(g_creates_done == 0);
  WaitUntil([] { return g_creates_started == 1; });
  CHECK(g_create_thread != Self());

  suppressor.Process(frame.data(), kFrame);
  CHECK(suppressor.Active());
  CHECK(!suppressor.StrongestActive());

  g_hold_create = false;
  suppressor.WaitForStrongestModel();
  suppressor.Process(frame.data(), kFrame);
  CHECK(suppressor.StrongestActive());
}

TEST(strongest_model_is_freed_off_the_audio_thread) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  ResetHooks();
  NoiseSuppressor suppressor;
  std::vector<float> frame = Frame();
  TakeTheModel(suppressor, frame);
  CHECK(suppressor.StrongestActive());

  suppressor.ConfigureStrongest(false);
  suppressor.Process(frame.data(), kFrame);
  WaitUntil([] { return g_frees_done == 1; });
  CHECK(g_frees_done == 1);
  CHECK(g_free_thread != Self());
  CHECK(!suppressor.StrongestActive());
}

TEST(strongest_model_that_fails_is_freed_off_the_audio_thread) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  ResetHooks();
  NoiseSuppressor suppressor;
  std::vector<float> frame = Frame();
  TakeTheModel(suppressor, frame);
  frame[3] = std::numeric_limits<float>::infinity();
  suppressor.Process(frame.data(), kFrame);
  WaitUntil([] { return g_frees_done == 1; });
  CHECK(g_frees_done == 1);
  CHECK(g_free_thread != Self());
  CHECK(!suppressor.StrongestActive());
}

TEST(strongest_switched_on_while_the_old_model_is_being_freed_loads_a_new_one) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  ResetHooks();
  NoiseSuppressor suppressor;
  std::vector<float> frame = Frame();
  TakeTheModel(suppressor, frame);
  suppressor.ConfigureStrongest(false);

  g_hold_free = true;
  suppressor.Process(frame.data(), kFrame);
  WaitUntil([] { return g_frees_started == 1; });
  suppressor.ConfigureStrongest(true);
  CHECK(g_creates_started == 1);

  g_hold_free = false;
  suppressor.WaitForStrongestModel();
  CHECK(g_creates_done == 2);
  suppressor.Process(frame.data(), kFrame);
  CHECK(suppressor.StrongestActive());
}

TEST(strongest_a_loaded_model_nobody_picked_up_is_freed_with_the_suppressor) {
  if (!DeepFilter::Compiled()) SKIP("built without the DeepFilterNet library");
  ResetHooks();
  {
    NoiseSuppressor suppressor;
    suppressor.ConfigureStrongest(true);
    suppressor.WaitForStrongestModel();
    CHECK(g_creates_done == 1);
    CHECK(g_frees_done == 0);
  }
  CHECK(g_frees_done == 1);
}
