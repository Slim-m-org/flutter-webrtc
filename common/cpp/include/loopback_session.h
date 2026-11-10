#ifndef FLUTTER_WEBRTC_PLUGIN_LOOPBACK_SESSION_H_
#define FLUTTER_WEBRTC_PLUGIN_LOOPBACK_SESSION_H_

#include <memory>
#include <mutex>
#include <utility>

#include "loopback_capturer.h"

namespace flutter_webrtc_plugin {

// A loopback capture and the share, identified by its capturer, it records for.
struct Loopback {
  std::unique_ptr<LoopbackCapturer> capturer;
  scoped_refptr<RTCAudioSource> source;
  const void* owner = nullptr;

  explicit operator bool() const { return capturer != nullptr; }

  void Stop() {
    if (capturer) {
      capturer->Stop();
    }
    capturer.reset();
    source = nullptr;
  }
};

// The loopback of the share in progress. Taking one out leaves the Stop()
// join to the caller, outside the lock.
class LoopbackSession {
 public:
  // Makes `next` current and returns the loopback it replaces.
  Loopback Replace(Loopback next) {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(current_, std::move(next));
  }

  // The current loopback if it records for `owner`, else an empty one.
  Loopback TakeFor(const void* owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_.capturer && current_.owner == owner) {
      return std::exchange(current_, Loopback());
    }
    return Loopback();
  }

  Loopback TakeAny() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(current_, Loopback());
  }

 private:
  std::mutex mutex_;
  Loopback current_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_LOOPBACK_SESSION_H_
