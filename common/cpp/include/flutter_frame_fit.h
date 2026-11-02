#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_FRAME_FIT_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_FRAME_FIT_H_

#include <cstdint>
#include <optional>
#include <vector>

#include <flutter/encodable_value.h>

namespace flutter_webrtc_plugin {

struct FrameSize {
  int width = 0;
  int height = 0;

  bool operator==(const FrameSize& other) const {
    return width == other.width && height == other.height;
  }
};

// The width and height a getDisplayMedia "video" constraint asks for. A bare
// number, or a map holding "exact", "ideal" or "max", sets a side; a side that
// is absent or not positive stays 0, which means unbounded. Returns nothing
// when neither side is set, so a caller that asks for no size gets frames
// untouched.
std::optional<FrameSize> RequestedFrameSize(
    const flutter::EncodableMap& video);

// The largest size with `source`'s aspect ratio that fits inside `limit`,
// never larger than `source`. Both sides are even, because I420 halves them for
// chroma. A zero side of `limit` is unbounded. Returns `source` itself when it
// already fits.
FrameSize FitWithin(FrameSize source, FrameSize limit);

// Read-only I420 planes, as libwebrtc hands them over.
struct I420View {
  int width = 0;
  int height = 0;
  const uint8_t* y = nullptr;
  int stride_y = 0;
  const uint8_t* u = nullptr;
  int stride_u = 0;
  const uint8_t* v = nullptr;
  int stride_v = 0;
};

// An owned, tightly packed I420 frame. Reused between frames, so scaling a
// steady stream allocates once.
struct ScaledI420 {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> y;
  std::vector<uint8_t> u;
  std::vector<uint8_t> v;

  int stride_y() const { return width; }
  int stride_uv() const { return (width + 1) / 2; }
};

// Scales every frame down to fit inside `limit` while keeping its aspect
// ratio. One instance serves one stream and one thread.
class FrameFitter {
 public:
  explicit FrameFitter(FrameSize limit) : limit_(limit) {}

  // Returns the scaled frame, or nullptr when `in` already fits and should
  // travel on untouched. The pointer stays valid until the next call.
  const ScaledI420* Fit(const I420View& in);

 private:
  FrameSize limit_;
  ScaledI420 out_;
  std::vector<uint16_t> scratch_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_FRAME_FIT_H_
