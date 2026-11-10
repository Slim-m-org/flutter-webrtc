#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "request_coalescer.h"

namespace {

using flutter_webrtc_plugin::RequestCoalescer;
using Stats = RequestCoalescer<std::pair<int, std::string>, int>;

TEST(RequestCoalescer_OnlyTheFirstRequestSchedulesARun) {
  Stats stats;
  CHECK(stats.Add({1, ""}, 10));
  CHECK(!stats.Add({1, ""}, 11));
  CHECK(!stats.Add({1, ""}, 12));
  CHECK((stats.Take({1, ""}) == std::vector<int>{10, 11, 12}));
}

TEST(RequestCoalescer_KeysDoNotShareARun) {
  Stats stats;
  CHECK(stats.Add({1, ""}, 10));
  CHECK(stats.Add({2, ""}, 20));
  CHECK(stats.Add({1, "track"}, 30));
  CHECK((stats.Take({2, ""}) == std::vector<int>{20}));
  CHECK((stats.Take({1, "track"}) == std::vector<int>{30}));
  CHECK((stats.Take({1, ""}) == std::vector<int>{10}));
}

TEST(RequestCoalescer_ARequestAfterTheRunStartsQueuesANewRun) {
  Stats stats;
  CHECK(stats.Add({1, ""}, 10));
  CHECK((stats.Take({1, ""}) == std::vector<int>{10}));
  CHECK(stats.Add({1, ""}, 11));
  CHECK((stats.Take({1, ""}) == std::vector<int>{11}));
}

TEST(RequestCoalescer_TakeOfAnUnknownKeyIsEmpty) {
  Stats stats;
  CHECK(stats.Take({9, "x"}).empty());
}

// Stats polls arriving behind one stalled collection: one queued run, not one
// per poll.
TEST(RequestCoalescer_PollsBehindAStallQueueOneRun) {
  Stats stats;
  int runs_scheduled = 0;
  for (int poll = 0; poll < 50; ++poll) {
    if (stats.Add({1, ""}, poll)) {
      ++runs_scheduled;
    }
  }
  CHECK(runs_scheduled == 1);
  CHECK(stats.Take({1, ""}).size() == 50);
}

}  // namespace
