#include "alive_guard.h"
#include "check.h"

namespace {

using flutter_webrtc_plugin::MakeAliveToken;
using flutter_webrtc_plugin::RunWhileAlive;

TEST(AliveGuard_RunsTheReplyWhileTheOwnerLives) {
  auto alive = MakeAliveToken();
  int runs = 0;
  RunWhileAlive(alive, [&] { ++runs; })();
  CHECK(runs == 1);
}

TEST(AliveGuard_DropsAReplyQueuedBeforeTheOwnerWasDestroyed) {
  auto alive = MakeAliveToken();
  int runs = 0;
  auto queued = RunWhileAlive(alive, [&] { ++runs; });
  *alive = false;
  queued();
  CHECK(runs == 0);
}

}  // namespace
