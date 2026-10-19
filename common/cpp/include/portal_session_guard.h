#ifndef FLUTTER_WEBRTC_PORTAL_SESSION_GUARD_H_
#define FLUTTER_WEBRTC_PORTAL_SESSION_GUARD_H_

#include <functional>

namespace flutter_webrtc_plugin {

// libwebrtc's screen media list starts an xdg-desktop-portal ScreenCast
// session of its own when it is first created, and the list is cached for the
// life of the process. That session is never used for capture, but it still
// shows the portal's chooser, so the first share asks twice: once for the list
// and once for the capturer. Only the Linux build (which defines
// FLUTTER_WEBRTC_PORTAL_GUARD) does anything here.
//
// Begin() runs right before a screen list is created. For the first list in
// the process, with [swallow] set, it drops that list's CreateSession call;
// the capturer's, which follows, is left alone. WhenSettled() runs [done]
// once the list's call has been dropped (or a short timeout has passed), so
// the caller can answer after the list's request is out of the way and
// before the capturer's is made.
#ifdef FLUTTER_WEBRTC_PORTAL_GUARD
class PortalSessionGuard {
 public:
  static void Begin(bool swallow);
  static void WhenSettled(std::function<void()> done);
};
#else
class PortalSessionGuard {
 public:
  static void Begin(bool) {}
  static void WhenSettled(std::function<void()> done) { done(); }
};
#endif

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PORTAL_SESSION_GUARD_H_
