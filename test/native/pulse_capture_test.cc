#include <pulse/error.h>
#include <pulse/simple.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "pulse_loopback_capturer.h"

namespace {

using flutter_webrtc_plugin::PulseLoopbackCapturer;
using libwebrtc::RTCAudioSource;
using Clock = std::chrono::steady_clock;

// Plays a steady tone into one named sink until destroyed.
class Tone {
 public:
  explicit Tone(const std::string& sink) {
    const pa_sample_spec spec = {PA_SAMPLE_S16LE, 48000, 2};
    int error = 0;
    simple_ = pa_simple_new(nullptr, "pulse_capture_test", PA_STREAM_PLAYBACK,
                            sink.c_str(), "tone", &spec, nullptr, nullptr,
                            &error);
    if (!simple_) return;
    thread_ = std::thread([this] {
      std::vector<int16_t> block(480 * 2);
      double phase = 0;
      while (!stop_) {
        for (size_t i = 0; i < 480; ++i) {
          const auto v = static_cast<int16_t>(12000 * std::sin(phase));
          block[2 * i] = block[2 * i + 1] = v;
          phase += 2 * M_PI * 440 / 48000;
        }
        if (pa_simple_write(simple_, block.data(), block.size() * 2,
                            nullptr) < 0) {
          break;
        }
      }
    });
  }

  ~Tone() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    if (simple_) pa_simple_free(simple_);
  }

  bool ok() const { return simple_ != nullptr; }

 private:
  pa_simple* simple_ = nullptr;
  std::thread thread_;
  std::atomic<bool> stop_{false};
};

bool WaitFor(const std::function<bool()>& done, int millis) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(millis);
  while (Clock::now() < deadline) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return done();
}

int Pactl(const std::string& args) {
  return std::system(("pactl " + args + " >/dev/null 2>&1").c_str());
}

std::string Pactl_Out(const std::string& args) {
  std::string out;
  FILE* pipe = popen(("pactl " + args + " 2>/dev/null").c_str(), "r");
  char buf[256];
  while (pipe && fgets(buf, sizeof buf, pipe)) out += buf;
  if (pipe) pclose(pipe);
  return out;
}

// Two private null sinks, "cap_a" and "cap_b", with cap_a the default.
struct Sinks {
  Sinks() {
    module_a = std::atoi(Pactl_Out("load-module module-null-sink sink_name=cap_a").c_str());
    module_b = std::atoi(Pactl_Out("load-module module-null-sink sink_name=cap_b").c_str());
    Pactl("set-default-sink cap_a");
  }
  ~Sinks() {
    if (module_b > 0) Pactl("unload-module " + std::to_string(module_b));
    if (module_a > 0) Pactl("unload-module " + std::to_string(module_a));
  }
  int module_a = 0;
  int module_b = 0;
};

bool ServerUp() {
  pa_simple* probe = nullptr;
  const pa_sample_spec spec = {PA_SAMPLE_S16LE, 48000, 2};
  int error = 0;
  probe = pa_simple_new(nullptr, "probe", PA_STREAM_PLAYBACK, nullptr, "probe",
                        &spec, nullptr, nullptr, &error);
  if (!probe) return false;
  pa_simple_free(probe);
  return true;
}

// A sink's playback buffer fills for a couple of seconds before it renders.
constexpr int kRenderWaitMillis = 10000;

TEST(PulseCapture_FollowsTheDefaultSinkWhenItChanges) {
  Sinks sinks;
  PulseLoopbackCapturer capturer("screen");
  auto source = std::make_shared<RTCAudioSource>();
  CHECK(capturer.Start(source));

  auto on_a = std::make_unique<Tone>("cap_a");
  CHECK(on_a->ok());
  CHECK(WaitFor([&] { return source->loud_frames_ > 20; }, kRenderWaitMillis));

  // Playback moves to the new default, so the old sink goes quiet.
  on_a.reset();
  Pactl("set-default-sink cap_b");
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  source->Reset();
  Tone on_b("cap_b");
  CHECK(on_b.ok());
  CHECK(WaitFor([&] { return source->loud_frames_ > 20; }, kRenderWaitMillis));
  capturer.Stop();
}

TEST(PulseCapture_KeepsCapturingWhenTheDefaultSinkIsRemoved) {
  Sinks sinks;
  PulseLoopbackCapturer capturer("screen");
  auto source = std::make_shared<RTCAudioSource>();
  CHECK(capturer.Start(source));

  Pactl("unload-module " + std::to_string(sinks.module_a));
  sinks.module_a = 0;
  Pactl("set-default-sink cap_b");
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  source->Reset();
  Tone on_b("cap_b");
  CHECK(on_b.ok());
  CHECK(WaitFor([&] { return source->loud_frames_ > 20; }, kRenderWaitMillis));
  CHECK(capturer.IsRunning());
  capturer.Stop();
}

TEST(PulseCapture_StopReturnsPromptlyWhileNothingPlays) {
  Sinks sinks;
  PulseLoopbackCapturer capturer("screen");
  auto source = std::make_shared<RTCAudioSource>();
  CHECK(capturer.Start(source));
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  const auto begin = Clock::now();
  capturer.Stop();
  const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
                        Clock::now() - begin)
                        .count();
  CHECK(took < 200);
  CHECK(!capturer.IsRunning());
}

// A pinned monitor is never retargeted, so losing it ends the capture.
TEST(PulseCapture_ReportsAnErrorWhenAPinnedMonitorGoesAway) {
  Sinks sinks;
  setenv("SLIMM_SHARE_AUDIO_SOURCE", "cap_a.monitor", 1);
  PulseLoopbackCapturer capturer("screen");
  std::atomic<int> errors{0};
  capturer.SetOnError([&] { ++errors; });
  auto source = std::make_shared<RTCAudioSource>();
  CHECK(capturer.Start(source));
  CHECK(capturer.IsRunning());

  Pactl("unload-module " + std::to_string(sinks.module_a));
  sinks.module_a = 0;
  CHECK(WaitFor([&] { return errors == 1; }, 3000));
  CHECK(!capturer.IsRunning());
  capturer.Stop();
  CHECK(errors == 1);
  unsetenv("SLIMM_SHARE_AUDIO_SOURCE");
}

TEST(PulseCapture_StartFailsWithoutAnErrorReportWhenTheMonitorIsMissing) {
  Sinks sinks;
  setenv("SLIMM_SHARE_AUDIO_SOURCE", "no_such_sink.monitor", 1);
  PulseLoopbackCapturer capturer("screen");
  std::atomic<int> errors{0};
  capturer.SetOnError([&] { ++errors; });
  const bool started = capturer.Start(std::make_shared<RTCAudioSource>());
  unsetenv("SLIMM_SHARE_AUDIO_SOURCE");
  if (started) SKIP("this server accepts a monitor that does not exist");
  CHECK(errors == 0);
}

}  // namespace

int main(int argc, char** argv) {
  ParseArgs(argc, argv);
  if (!ServerUp()) return SuiteSkipped("no PulseAudio or PipeWire server");
  RunRegistry();
  return Finish();
}
