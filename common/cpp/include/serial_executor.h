#ifndef FLUTTER_WEBRTC_PLUGIN_SERIAL_EXECUTOR_H_
#define FLUTTER_WEBRTC_PLUGIN_SERIAL_EXECUTOR_H_

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace flutter_webrtc_plugin {

// One worker thread running posted tasks in order. Stop(), also run by the
// destructor, drops the queued tasks and waits for the running one.
class SerialExecutor {
 public:
  SerialExecutor() = default;
  ~SerialExecutor() { Stop(); }
  SerialExecutor(const SerialExecutor&) = delete;
  SerialExecutor& operator=(const SerialExecutor&) = delete;

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
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        if (stopping_) {
          return;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
      }
      task();
    }
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  std::thread thread_;
  bool stopping_ = false;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_SERIAL_EXECUTOR_H_
