#include "audio_server_probe.h"

#ifdef HAVE_LIBPULSE
#include <pulse/context.h>
#include <pulse/mainloop.h>

#include <chrono>
#include <thread>
#endif

namespace flutter_webrtc_plugin {

#ifdef HAVE_LIBPULSE

namespace {
constexpr auto kProbeTimeout = std::chrono::seconds(3);
}  // namespace

bool AudioServerReachable() {
  pa_mainloop* mainloop = pa_mainloop_new();
  if (!mainloop) {
    return true;
  }
  pa_context* context =
      pa_context_new(pa_mainloop_get_api(mainloop), "flutter-webrtc-probe");
  bool ready = false;
  if (context &&
      pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) >=
          0) {
    const auto deadline = std::chrono::steady_clock::now() + kProbeTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
      pa_mainloop_iterate(mainloop, 0, nullptr);
      const pa_context_state_t state = pa_context_get_state(context);
      if (state == PA_CONTEXT_READY) {
        ready = true;
        break;
      }
      if (!PA_CONTEXT_IS_GOOD(state)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  if (context) {
    pa_context_disconnect(context);
    pa_context_unref(context);
  }
  pa_mainloop_free(mainloop);
  return ready;
}

#else

bool AudioServerReachable() { return true; }

#endif

}  // namespace flutter_webrtc_plugin
