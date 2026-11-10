#include <gio/gio.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "portal_session_guard.h"

namespace {

using flutter_webrtc_plugin::PortalSessionGuard;

// The guard arms once per process, so everything it does is one sequence: a
// first Wayland list parks replies, and a share start must still answer them.
TEST(PortalSessionGuard_DisarmAnswersEveryParkedListReply) {
  std::vector<std::string> answered;
  setenv("XDG_SESSION_TYPE", "wayland", 1);

  PortalSessionGuard::Begin(true);
  PortalSessionGuard::WhenSettled([&] { answered.push_back("first list"); });
  PortalSessionGuard::WhenSettled([&] { answered.push_back("second list"); });
  CHECK(answered.empty());

  PortalSessionGuard::Disarm();
  while (g_main_context_iteration(nullptr, FALSE)) {
  }
  CHECK((answered == std::vector<std::string>{"first list", "second list"}));

  PortalSessionGuard::Disarm();
  PortalSessionGuard::WhenSettled([&] { answered.push_back("after"); });
  CHECK(answered.size() == 3);
}

}  // namespace

// Runs the tests on a private session bus, the one the guard's filter sits on.
int main() {
  if (g_find_program_in_path("dbus-daemon") == nullptr) {
    std::printf("skip: no dbus-daemon to start a private session bus\n");
    return 0;
  }
  GTestDBus* bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(bus);
  g_main_context_acquire(g_main_context_default());

  for (const TestCase& test : Registry()) {
    const int before = Failures();
    test.body();
    std::printf("%s %s\n", Failures() == before ? "ok  " : "FAIL", test.name);
  }

  g_test_dbus_down(bus);
  g_object_unref(bus);
  std::printf("%d failure(s)\n", Failures());
  return Failures() == 0 ? 0 : 1;
}
