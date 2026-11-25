#include <atomic>
#include <chrono>
#include <thread>

#include "check.h"
#include "trailing_throttle.h"

namespace {

using flutter_webrtc_plugin::TrailingThrottle;
using std::chrono::milliseconds;

constexpr milliseconds kInterval(60);

bool WaitFor(const std::atomic<int>& count, int want) {
  for (int i = 0; i < 200 && count.load() < want; i++) {
    std::this_thread::sleep_for(milliseconds(10));
  }
  return count.load() >= want;
}

TEST(TrailingThrottle_ARequestOutsideTheIntervalPublishesAtOnce) {
  std::atomic<int> published{0};
  TrailingThrottle throttle([&] { published++; });
  throttle.SetInterval(kInterval);
  throttle.Request();
  CHECK(published.load() == 1);
}

TEST(TrailingThrottle_AHeldRequestIsPublishedOnceTheIntervalElapses) {
  std::atomic<int> published{0};
  TrailingThrottle throttle([&] { published++; });
  throttle.SetInterval(kInterval);
  throttle.Request();
  throttle.Request();
  CHECK(published.load() == 1);
  CHECK(WaitFor(published, 2));
}

TEST(TrailingThrottle_ABurstEndsOnOneTrailingPublish) {
  std::atomic<int> published{0};
  TrailingThrottle throttle([&] { published++; });
  throttle.SetInterval(kInterval);
  for (int i = 0; i < 10; i++) throttle.Request();
  CHECK(WaitFor(published, 2));
  std::this_thread::sleep_for(kInterval * 3);
  CHECK(published.load() == 2);
}

TEST(TrailingThrottle_ZeroIntervalPublishesEveryRequest) {
  std::atomic<int> published{0};
  TrailingThrottle throttle([&] { published++; });
  for (int i = 0; i < 5; i++) throttle.Request();
  CHECK(published.load() == 5);
}

TEST(TrailingThrottle_ACancelledRequestIsNeverPublished) {
  std::atomic<int> published{0};
  TrailingThrottle throttle([&] { published++; });
  throttle.SetInterval(kInterval);
  throttle.Request();
  throttle.Request();
  throttle.Cancel();
  std::this_thread::sleep_for(kInterval * 3);
  CHECK(published.load() == 1);
}

TEST(TrailingThrottle_DestroyingItWithARequestHeldDoesNotPublish) {
  std::atomic<int> published{0};
  {
    TrailingThrottle throttle([&] { published++; });
    throttle.SetInterval(kInterval);
    throttle.Request();
    throttle.Request();
  }
  CHECK(published.load() == 1);
}

}  // namespace
