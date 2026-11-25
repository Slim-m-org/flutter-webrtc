#ifndef TEST_STUB_RTC_AUDIO_TRACK_H_
#define TEST_STUB_RTC_AUDIO_TRACK_H_

#include "rtc_types.h"

// Just SetVolume, so the native tests build without downloading libwebrtc.
namespace libwebrtc {
class RTCAudioTrack {
 public:
  virtual ~RTCAudioTrack() = default;
  virtual void SetVolume(double volume) = 0;
};
}  // namespace libwebrtc

#endif  // TEST_STUB_RTC_AUDIO_TRACK_H_
