#ifndef TEST_PULSE_STUB_RTC_AUDIO_SOURCE_H_
#define TEST_PULSE_STUB_RTC_AUDIO_SOURCE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstdlib>

#include "rtc_types.h"

// An audio source that records what the capturer delivers, so the Pulse tests
// build without libwebrtc.
namespace libwebrtc {
class RTCAudioSource {
 public:
  virtual ~RTCAudioSource() = default;

  void CaptureFrame(const void* data, int /*bits*/, int /*rate*/,
                    size_t channels, size_t frames) {
    const auto* samples = static_cast<const int16_t*>(data);
    int peak = 0;
    for (size_t i = 0; i < channels * frames; ++i) {
      peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
    }
    ++frames_;
    if (peak > 0) ++loud_frames_;
    int seen = peak_;
    while (peak > seen && !peak_.compare_exchange_weak(seen, peak)) {
    }
  }

  void Reset() {
    frames_ = 0;
    loud_frames_ = 0;
    peak_ = 0;
  }

  std::atomic<int> frames_{0};
  std::atomic<int> loud_frames_{0};
  std::atomic<int> peak_{0};
};
}  // namespace libwebrtc

#endif  // TEST_PULSE_STUB_RTC_AUDIO_SOURCE_H_
