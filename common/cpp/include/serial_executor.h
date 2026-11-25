#ifndef FLUTTER_WEBRTC_PLUGIN_SERIAL_EXECUTOR_H_
#define FLUTTER_WEBRTC_PLUGIN_SERIAL_EXECUTOR_H_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace flutter_webrtc_plugin {

// One worker thread running posted tasks in order. Stop(), also run by the
// destructor, drops the queued tasks and waits for the running one.
//
// A wake handler, set before the first Post, runs on the worker after every
// Wake() and at least every kTick, so a real-time thread can hand work over
// without the lock and allocation that Post takes.
class SerialExecutor {
 public:
  SerialExecutor() = default;
  ~SerialExecutor() { Stop(); }
  SerialExecutor(const SerialExecutor&) = delete;
  SerialExecutor& operator=(const SerialExecutor&) = delete;

  static constexpr std::chrono::milliseconds kTick{100};

  void SetWakeHandler(std::function<void()> handler) {
    wake_handler_ = std::move(handler);
  }

  // Safe on a real-time thread: no lock, no allocation. Does nothing until
  // the first Post has started the worker; the tick covers a missed notify.
  void Wake() {
    if (!wake_handler_) return;
    wake_.store(true, std::memory_order_release);
    cv_.notify_one();
  }

  // Returns false, and drops `task`, once stopped.
  bool Post(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) {
        return false;
      }
      tasks_.push_back(std::move(task));
      if (!thread_.joinable()) {
        thread_ = std::thread([this] { Run(); });
      }
    }
    cv_.notify_one();
    return true;
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    std::deque<std::function<void()>> dropped;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      dropped.swap(tasks_);
    }
  }

 private:
  void Run() {
    for (;;) {
      std::function<void()> task;
      bool tick = false;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        auto ready = [this] {
          return stopping_ || !tasks_.empty() ||
                 wake_.load(std::memory_order_acquire);
        };
        if (wake_handler_) {
          tick = !cv_.wait_for(lock, kTick, ready);
        } else {
          cv_.wait(lock, ready);
        }
        if (stopping_) {
          return;
        }
        if (!tasks_.empty()) {
          task = std::move(tasks_.front());
          tasks_.pop_front();
        }
      }
      if (wake_handler_ && (tick || wake_.exchange(false))) wake_handler_();
      if (task) task();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  std::thread thread_;
  bool stopping_ = false;
  std::atomic<bool> wake_{false};
  std::function<void()> wake_handler_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_SERIAL_EXECUTOR_H_
