#include "check.h"
#include "probe_memo.h"

namespace {

using flutter_webrtc_plugin::ProbeMemo;
using std::chrono::seconds;

TEST(ProbeMemo_RepeatedCallsAfterAFailureDoNotProbeAgain) {
  ProbeMemo memo(seconds(5));
  const ProbeMemo::Clock::time_point start{};
  int probes = 0;
  auto failing = [&probes] {
    ++probes;
    return false;
  };
  for (int call = 0; call < 20; ++call) {
    CHECK(!memo.Run(failing, start + seconds(call % 5)));
  }
  CHECK(probes == 1);
}

TEST(ProbeMemo_ProbesAgainOnceTheFailureExpires) {
  ProbeMemo memo(seconds(5));
  const ProbeMemo::Clock::time_point start{};
  int probes = 0;
  auto failing = [&probes] {
    ++probes;
    return false;
  };
  CHECK(!memo.Run(failing, start));
  CHECK(!memo.Run(failing, start + seconds(6)));
  CHECK(probes == 2);
}

TEST(ProbeMemo_ASuccessIsNotRemembered) {
  ProbeMemo memo(seconds(5));
  const ProbeMemo::Clock::time_point start{};
  int probes = 0;
  auto passing = [&probes] {
    ++probes;
    return true;
  };
  CHECK(memo.Run(passing, start));
  CHECK(memo.Run(passing, start));
  CHECK(probes == 2);
}

TEST(ProbeMemo_RecoveryAfterExpiryClearsTheFailure) {
  ProbeMemo memo(seconds(5));
  const ProbeMemo::Clock::time_point start{};
  CHECK(!memo.Run([] { return false; }, start));
  CHECK(memo.Run([] { return true; }, start + seconds(6)));
  int probes = 0;
  CHECK(memo.Run([&probes] { ++probes; return true; }, start + seconds(7)));
  CHECK(probes == 1);
}

}  // namespace
