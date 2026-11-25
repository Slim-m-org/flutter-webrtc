#ifndef FLUTTER_WEBRTC_PLUGIN_ALIVE_GUARD_H_
#define FLUTTER_WEBRTC_PLUGIN_ALIVE_GUARD_H_

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace flutter_webrtc_plugin {

using AliveToken = std::shared_ptr<std::atomic<bool>>;

inline AliveToken MakeAliveToken() {
  return std::make_shared<std::atomic<bool>>(true);
}

// Wraps `reply` so it does nothing once its owner has cleared `alive`.
inline std::function<void()> RunWhileAlive(AliveToken alive,
                                           std::function<void()> reply) {
  return [alive = std::move(alive), reply = std::move(reply)]() {
    if (alive->load()) reply();
  };
}

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_ALIVE_GUARD_H_
