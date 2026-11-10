#ifndef FLUTTER_WEBRTC_PLUGIN_LOCKED_MAP_H_
#define FLUTTER_WEBRTC_PLUGIN_LOCKED_MAP_H_

#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace flutter_webrtc_plugin {

// A map that exists only behind its own mutex, so no writer can skip the lock.
// Values come back as copies, and nothing runs under the lock.
template <typename Key, typename Value>
class LockedMap {
 public:
  void Set(const Key& key, Value value) {
    std::lock_guard<std::mutex> lock(mutex_);
    map_[key] = std::move(value);
  }

  void Erase(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    map_.erase(key);
  }

  // A default-constructed Value (a null scoped_refptr) when `key` is absent.
  Value Find(const Key& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = map_.find(key);
    return found != map_.end() ? found->second : Value();
  }

  std::vector<Value> Values() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Value> values;
    values.reserve(map_.size());
    for (const auto& entry : map_) {
      values.push_back(entry.second);
    }
    return values;
  }

 private:
  mutable std::mutex mutex_;
  std::map<Key, Value> map_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_LOCKED_MAP_H_
