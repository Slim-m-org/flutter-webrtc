#ifndef FLUTTER_WEBRTC_SCREEN_SESSION_ENV_H_
#define FLUTTER_WEBRTC_SCREEN_SESSION_ENV_H_

#include <cstdlib>

namespace flutter_webrtc_plugin {

// libwebrtc picks the portal screen capturer only when XDG_SESSION_TYPE says
// wayland, and otherwise opens an X display; with no DISPLAY that yields no
// capturer and the process segfaults. Returns false when no screen backend
// exists, and otherwise makes sure the portal is chosen when X is absent.
inline bool PrepareScreenSession() {
  const char* display = std::getenv("DISPLAY");
  if (display && *display) {
    return true;
  }
  const char* wayland = std::getenv("WAYLAND_DISPLAY");
  if (!wayland || !*wayland) {
    return false;
  }
  setenv("XDG_SESSION_TYPE", "wayland", 1);
  return true;
}

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_SCREEN_SESSION_ENV_H_
