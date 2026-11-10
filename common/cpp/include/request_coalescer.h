#ifndef FLUTTER_WEBRTC_PLUGIN_REQUEST_COALESCER_H_
#define FLUTTER_WEBRTC_PLUGIN_REQUEST_COALESCER_H_

#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace flutter_webrtc_plugin {

// Requests for the same key that arrive before its run starts share that run,
// so a slow call is never queued more than once per key.
template <typename Key, typename Waiter>
class RequestCoalescer {
 public:
  // True when `waiter` is the first for `key`, so the caller schedules a run.
  bool Add(const Key& key, Waiter waiter) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& waiters = queued_[key];
    waiters.push_back(std::move(waiter));
    return waiters.size() == 1;
  }

  // Called as the run starts; a request after this queues a new run.
  std::vector<Waiter> Take(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = queued_.find(key);
    if (found == queued_.end()) {
      return {};
    }
    std::vector<Waiter> waiters = std::move(found->second);
    queued_.erase(found);
    return waiters;
  }

 private:
  std::mutex mutex_;
  std::map<Key, std::vector<Waiter>> queued_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_REQUEST_COALESCER_H_
