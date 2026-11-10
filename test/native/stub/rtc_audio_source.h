#ifndef TEST_STUB_RTC_AUDIO_SOURCE_H_
#define TEST_STUB_RTC_AUDIO_SOURCE_H_

#include "rtc_types.h"

// An empty audio source, so the native tests build without downloading
// libwebrtc.
namespace libwebrtc {
class RTCAudioSource {
 public:
  virtual ~RTCAudioSource() = default;
};
}  // namespace libwebrtc

#endif  // TEST_STUB_RTC_AUDIO_SOURCE_H_
