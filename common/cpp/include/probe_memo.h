#ifndef FLUTTER_WEBRTC_PLUGIN_PROBE_MEMO_H_
#define FLUTTER_WEBRTC_PLUGIN_PROBE_MEMO_H_

#include <chrono>
#include <functional>

namespace flutter_webrtc_plugin {

// Remembers a failed probe for `ttl`, so calls arriving while it keeps failing
// return at once instead of each paying for the probe again.
class ProbeMemo {
 public:
  using Clock = std::chrono::steady_clock;

  explicit ProbeMemo(Clock::duration ttl) : ttl_(ttl) {}

  bool Run(const std::function<bool()>& probe,
           Clock::time_point now = Clock::now()) {
    if (failed_ && now - failed_at_ < ttl_) {
      return false;
    }
    failed_ = !probe();
    failed_at_ = now;
    return !failed_;
  }

 private:
  Clock::duration ttl_;
  bool failed_ = false;
  Clock::time_point failed_at_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_PROBE_MEMO_H_
