#ifndef FLUTTER_WEBRTC_PLUGIN_TRAILING_THROTTLE_H_
#define FLUTTER_WEBRTC_PLUGIN_TRAILING_THROTTLE_H_

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace flutter_webrtc_plugin {

// Rate-limits a publish callback and always ends on the latest request: a
// request inside the interval is held and published when the interval has
// elapsed, so a source that stops changing still settles on its last state.
class TrailingThrottle {
 public:
  using Publish = std::function<void()>;

  explicit TrailingThrottle(Publish publish) : publish_(std::move(publish)) {}

  ~TrailingThrottle() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
      pending_ = false;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  TrailingThrottle(const TrailingThrottle&) = delete;
  TrailingThrottle& operator=(const TrailingThrottle&) = delete;

  // Zero publishes every request at once.
  void SetInterval(std::chrono::microseconds interval) {
    std::lock_guard<std::mutex> lock(mutex_);
    interval_ = interval;
  }

  void Request() {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto now = Clock::now();
    if (interval_.count() == 0 || now - last_ >= interval_) {
      pending_ = false;
      last_ = now;
      publish_();
      return;
    }
    pending_ = true;
    if (!worker_.joinable()) worker_ = std::thread([this] { Run(); });
    wake_.notify_all();
  }

  // Drops a held request; none is published after this returns.
  void Cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = false;
  }

 private:
  using Clock = std::chrono::steady_clock;

  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
      if (!pending_) {
        wake_.wait(lock);
        continue;
      }
      const auto due = last_ + interval_;
      if (Clock::now() < due) {
        wake_.wait_until(lock, due);
        continue;
      }
      pending_ = false;
      last_ = Clock::now();
      publish_();
    }
  }

  const Publish publish_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::chrono::microseconds interval_{0};
  Clock::time_point last_{};
  bool pending_ = false;
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_TRAILING_THROTTLE_H_
