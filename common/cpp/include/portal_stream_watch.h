#ifndef FLUTTER_WEBRTC_PORTAL_STREAM_WATCH_H_
#define FLUTTER_WEBRTC_PORTAL_STREAM_WATCH_H_

#include <optional>

namespace flutter_webrtc_plugin {

// Where the monitor the user picked in the xdg-desktop-portal chooser sits,
// in the compositor's logical coordinates.
struct PortalStreamGeometry {
  int x;
  int y;
  int width;
  int height;
};

// libwebrtc reads the ScreenCast portal's Start reply itself and keeps only
// the PipeWire node, so which monitor was picked never reaches the app. The
// reply also carries that monitor's position and size; this watches the
// session bus for it. Arm() runs as a share starts and forgets the last one,
// Last() answers once the portal has replied for a monitor share. Only the
// Linux build (which defines FLUTTER_WEBRTC_PORTAL_GUARD) does anything.
#ifdef FLUTTER_WEBRTC_PORTAL_GUARD
class PortalStreamWatch {
 public:
  static void Arm();
  static std::optional<PortalStreamGeometry> Last();
};
#else
class PortalStreamWatch {
 public:
  static void Arm() {}
  static std::optional<PortalStreamGeometry> Last() { return std::nullopt; }
};
#endif

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PORTAL_STREAM_WATCH_H_
