#ifndef FLUTTER_WEBRTC_PLUGIN_BOUNDED_TASK_H_
#define FLUTTER_WEBRTC_PLUGIN_BOUNDED_TASK_H_

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace flutter_webrtc_plugin {

// Runs `work` on its own thread and waits at most `timeout` for its result.
// A task that finishes after the caller gave up hands its result to `discard`
// on its own thread, so nothing it produced is leaked and nothing the caller
// owns is touched. The thread shares its state with the caller by ownership.
template <typename T>
std::optional<T> RunBounded(std::chrono::milliseconds timeout,
                            std::function<T()> work,
                            std::function<void(T&)> discard) {
  struct State {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<T> value;
    bool abandoned = false;
  };
  auto state = std::make_shared<State>();
  std::thread([state, work = std::move(work), discard = std::move(discard)] {
    T value = work();
    std::unique_lock<std::mutex> lock(state->mutex);
    if (state->abandoned) {
      lock.unlock();
      discard(value);
      return;
    }
    state->value = std::move(value);
    state->cv.notify_all();
  }).detach();

  std::unique_lock<std::mutex> lock(state->mutex);
  state->cv.wait_for(lock, timeout, [&] { return state->value.has_value(); });
  if (!state->value) {
    state->abandoned = true;
    return std::nullopt;
  }
  return std::move(state->value);
}

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_BOUNDED_TASK_H_
