#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "check.h"
#include "loopback_session.h"

namespace {

using flutter_webrtc_plugin::Loopback;
using flutter_webrtc_plugin::LoopbackCapturer;
using flutter_webrtc_plugin::LoopbackSession;

// What a capturer saw, kept after the capturer itself is destroyed.
struct Counts {
  std::atomic<int> stops{0};
  std::atomic<int> overlapping_stops{0};
  std::atomic<bool> stopping{false};
};

class FakeCapturer : public LoopbackCapturer {
 public:
  explicit FakeCapturer(std::shared_ptr<Counts> counts)
      : counts_(std::move(counts)) {}

  bool Start(libwebrtc::scoped_refptr<libwebrtc::RTCAudioSource>) override {
    return true;
  }

  void Stop() override {
    if (counts_->stopping.exchange(true)) {
      ++counts_->overlapping_stops;
    }
    ++counts_->stops;
    counts_->stopping = false;
  }

 private:
  std::shared_ptr<Counts> counts_;
};

Loopback Start(const void* owner, const std::shared_ptr<Counts>& counts) {
  Loopback loopback;
  loopback.capturer = std::make_unique<FakeCapturer>(counts);
  loopback.owner = owner;
  return loopback;
}

TEST(LoopbackSession_TakeForOnlyYieldsTheLoopbackOfThatOwner) {
  int share_a = 0;
  int share_b = 0;
  auto counts = std::make_shared<Counts>();
  LoopbackSession session;
  CHECK(!session.Replace(Start(&share_a, counts)));
  CHECK(!session.TakeFor(&share_b));
  Loopback taken = session.TakeFor(&share_a);
  CHECK(taken);
  CHECK(counts->stops == 0);
  taken.Stop();
  CHECK(counts->stops == 1);
  CHECK(!session.TakeFor(&share_a));
}

// A share restarts: the previous capturer's stop event arrives after the new
// share's loopback replaced the old one, and must leave the new one running.
TEST(LoopbackSession_AnEarlierSharesStopLeavesTheNewLoopbackRunning) {
  int share_a = 0;
  int share_b = 0;
  auto first = std::make_shared<Counts>();
  auto second = std::make_shared<Counts>();
  LoopbackSession session;
  session.Replace(Start(&share_a, first));
  session.Replace(Start(&share_b, second)).Stop();
  CHECK(first->stops == 1);

  session.TakeFor(&share_a).Stop();
  CHECK(second->stops == 0);
  CHECK(session.TakeFor(&share_b));
}

TEST(LoopbackSession_ReplaceHandsBackThePreviousLoopbackToStop) {
  int share_a = 0;
  int share_b = 0;
  auto first = std::make_shared<Counts>();
  auto second = std::make_shared<Counts>();
  LoopbackSession session;
  session.Replace(Start(&share_a, first));
  Loopback replaced = session.Replace(Start(&share_b, second));
  CHECK(replaced);
  CHECK(first->stops == 0);
  replaced.Stop();
  CHECK(first->stops == 1);
  CHECK(!replaced);
  CHECK(second->stops == 0);
}

TEST(LoopbackSession_TakeAnyEmptiesTheSession) {
  int share = 0;
  auto counts = std::make_shared<Counts>();
  LoopbackSession session;
  CHECK(!session.TakeAny());
  session.Replace(Start(&share, counts));
  CHECK(session.TakeAny());
  CHECK(!session.TakeAny());
  CHECK(!session.TakeFor(&share));
}

// The platform thread restarts shares while libwebrtc's thread delivers the
// stop of earlier ones. Every loopback must be stopped exactly once, and never
// by two threads at once. Run under -fsanitize=thread to also catch
// unsynchronised access to the session itself.
TEST(LoopbackSession_RestartsAgainstStopsStopEachLoopbackOnce) {
  constexpr int kShares = 4000;
  std::vector<int> shares(kShares);
  std::vector<std::shared_ptr<Counts>> counts(kShares);
  for (auto& entry : counts) {
    entry = std::make_shared<Counts>();
  }
  LoopbackSession session;
  std::atomic<int> started{0};

  std::thread platform([&] {
    for (int i = 0; i < kShares; ++i) {
      session.Replace(Start(&shares[i], counts[i])).Stop();
      started = i + 1;
    }
  });
  std::thread libwebrtc([&] {
    for (int seen = 0; seen < kShares;) {
      const int limit = started;
      for (; seen < limit; ++seen) {
        session.TakeFor(&shares[seen]).Stop();
      }
    }
  });
  platform.join();
  libwebrtc.join();
  session.TakeAny().Stop();

  int stopped_once = 0;
  int overlapping = 0;
  for (const auto& entry : counts) {
    stopped_once += entry->stops == 1 ? 1 : 0;
    overlapping += entry->overlapping_stops;
  }
  CHECK(stopped_once == kShares);
  CHECK(overlapping == 0);
}

}  // namespace
