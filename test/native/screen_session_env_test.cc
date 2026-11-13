#include <string>
#include <cstdlib>

#include "check.h"
#include "screen_session_env.h"

namespace {

using flutter_webrtc_plugin::PrepareScreenSession;

void SetEnv(const char* name, const char* value) {
  if (value) {
    setenv(name, value, 1);
  } else {
    unsetenv(name);
  }
}

void Session(const char* display, const char* wayland, const char* type) {
  SetEnv("DISPLAY", display);
  SetEnv("WAYLAND_DISPLAY", wayland);
  SetEnv("XDG_SESSION_TYPE", type);
}

TEST(ScreenSession_WaylandWithoutXSelectsThePortalEvenWithoutASessionType) {
  Session(nullptr, "wayland-0", nullptr);
  CHECK(PrepareScreenSession());
  CHECK(std::string(std::getenv("XDG_SESSION_TYPE")) == "wayland");
  Session(nullptr, "wayland-0", "tty");
  CHECK(PrepareScreenSession());
  CHECK(std::string(std::getenv("XDG_SESSION_TYPE")) == "wayland");
}

TEST(ScreenSession_NoDisplayAtAllHasNoScreenBackend) {
  Session(nullptr, nullptr, nullptr);
  CHECK(!PrepareScreenSession());
  Session("", "", "x11");
  CHECK(!PrepareScreenSession());
}

TEST(ScreenSession_AnXDisplayLeavesTheSessionAlone) {
  Session(":0", "wayland-0", "x11");
  CHECK(PrepareScreenSession());
  CHECK(std::string(std::getenv("XDG_SESSION_TYPE")) == "x11");
}

}  // namespace
