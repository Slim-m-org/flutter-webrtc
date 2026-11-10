#ifndef TEST_STUB_RTC_TYPES_H_
#define TEST_STUB_RTC_TYPES_H_

#include <memory>

// Just the smart pointer, so the native tests build without downloading
// libwebrtc.
namespace libwebrtc {
template <typename T>
using scoped_refptr = std::shared_ptr<T>;
}  // namespace libwebrtc

#endif  // TEST_STUB_RTC_TYPES_H_
