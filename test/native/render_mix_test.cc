#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "check.h"
#include "flutter_render_limiter.h"
#include "flutter_render_mix.h"

namespace {

using flutter_webrtc_plugin::RenderMix;
using namespace std::chrono_literals;

// Stands in for a track whose SetVolume waits on another libwebrtc thread: while it runs,
// a second thread asks the mix for something, as the signaling thread does on OnTrack.
class ProbingTrack : public libwebrtc::RTCAudioTrack {
 public:
  void SetVolume(double volume) override {
    last_volume = volume;
    ++calls;
    if (probing.exchange(true)) return;
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    other = std::thread([done] {
      RenderMix::Instance()->TrackRemoved("probe-only");
      done->set_value();
    });
    blocked_other_thread = finished.wait_for(2s) != std::future_status::ready;
  }

  // Once the mix call that ran SetVolume has returned and let go of its lock.
  void Join() {
    if (other.joinable()) other.join();
    probing = false;
  }

  std::thread other;

  std::atomic<bool> probing{false};
  bool blocked_other_thread = false;
  double last_volume = -1.0;
  int calls = 0;
};

class CountingTrack : public libwebrtc::RTCAudioTrack {
 public:
  void SetVolume(double volume) override {
    last_volume = volume;
    ++calls;
  }
  double last_volume = -1.0;
  int calls = 0;
};

}  // namespace

TEST(RenderMix_SetVolumeRunsWithTheMixUnlocked) {
  RenderMix* mix = RenderMix::Instance();
  auto track = std::make_shared<ProbingTrack>();
  bool blocked = false;
  mix->TrackAdded("mix-test-a", track);
  CHECK(track->calls >= 1);
  track->Join();
  blocked = blocked || track->blocked_other_thread;
  CHECK(mix->SetGain("mix-test-a", 1.5));
  track->Join();
  blocked = blocked || track->blocked_other_thread;
  mix->TrackRemoved("mix-test-a");
  track->Join();
  CHECK(!blocked);
}

TEST(RenderMix_GainReachesTheTrackAndTheLimiterBoost) {
  RenderMix* mix = RenderMix::Instance();
  auto loud = std::make_shared<CountingTrack>();
  auto other = std::make_shared<CountingTrack>();
  mix->TrackAdded("mix-test-b", loud);
  mix->TrackAdded("mix-test-c", other);
  CHECK(mix->SetGain("mix-test-b", 2.0));
  CHECK(flutter_webrtc_plugin::RenderLimiter::Instance()->Boost() == 2.0f);
  CHECK(loud->last_volume == 1.0);
  CHECK(other->last_volume == 0.5);
  CHECK(!mix->SetGain("mix-test-missing", 1.0));
  mix->TrackRemoved("mix-test-b");
  CHECK(flutter_webrtc_plugin::RenderLimiter::Instance()->Boost() == 1.0f);
  CHECK(other->last_volume == 1.0);
  mix->TrackRemoved("mix-test-c");
}
