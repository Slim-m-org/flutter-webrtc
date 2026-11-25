#ifndef FLUTTER_WEBRTC_PLUGIN_LOOPBACK_DOWNMIX_H_
#define FLUTTER_WEBRTC_PLUGIN_LOOPBACK_DOWNMIX_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flutter_webrtc_plugin {

// How much of one source channel goes into the left and right output.
struct DownmixGain {
  float left = 0.0f;
  float right = 0.0f;
};

namespace loopback_downmix_internal {

constexpr float kMinus3dB = 0.70710678f;

// Speaker bits of WAVEFORMATEXTENSIBLE.dwChannelMask, in channel order.
enum Speaker : uint32_t {
  kFrontLeft = 0x1,
  kFrontRight = 0x2,
  kFrontCenter = 0x4,
  kLowFrequency = 0x8,
  kBackLeft = 0x10,
  kBackRight = 0x20,
  kFrontLeftOfCenter = 0x40,
  kFrontRightOfCenter = 0x80,
  kBackCenter = 0x100,
  kSideLeft = 0x200,
  kSideRight = 0x400,
};

// The layout Windows reports for a plain channel count.
inline uint32_t DefaultMask(size_t channels) {
  switch (channels) {
    case 2: return kFrontLeft | kFrontRight;
    case 3: return kFrontLeft | kFrontRight | kFrontCenter;
    case 4: return kFrontLeft | kFrontRight | kBackLeft | kBackRight;
    case 5:
      return kFrontLeft | kFrontRight | kFrontCenter | kBackLeft | kBackRight;
    case 6:
      return kFrontLeft | kFrontRight | kFrontCenter | kLowFrequency |
             kBackLeft | kBackRight;
    case 7:
      return kFrontLeft | kFrontRight | kFrontCenter | kLowFrequency |
             kBackLeft | kBackRight | kBackCenter;
    case 8:
      return kFrontLeft | kFrontRight | kFrontCenter | kLowFrequency |
             kBackLeft | kBackRight | kSideLeft | kSideRight;
    default: return kFrontLeft | kFrontRight;
  }
}

inline DownmixGain GainOf(uint32_t speaker) {
  switch (speaker) {
    case kFrontLeft: return {1.0f, 0.0f};
    case kFrontRight: return {0.0f, 1.0f};
    case kFrontCenter: return {kMinus3dB, kMinus3dB};
    case kBackCenter: return {0.5f, 0.5f};
    case kBackLeft:
    case kSideLeft:
    case kFrontLeftOfCenter: return {kMinus3dB, 0.0f};
    case kBackRight:
    case kSideRight:
    case kFrontRightOfCenter: return {0.0f, kMinus3dB};
    default: return {};
  }
}

}  // namespace loopback_downmix_internal

// One gain pair per channel of an interleaved frame, by ITU-R BS.775: the
// centre and the surrounds at -3 dB into the nearer sides, the LFE dropped.
inline std::vector<DownmixGain> StereoDownmixGains(size_t channels,
                                                   uint32_t channel_mask) {
  using namespace loopback_downmix_internal;
  if (channels == 1) {
    return {{1.0f, 1.0f}};
  }
  uint32_t mask = channel_mask;
  size_t bits = 0;
  for (uint32_t m = mask; m != 0; m &= m - 1) {
    ++bits;
  }
  if (bits != channels) {
    mask = DefaultMask(channels);
  }
  std::vector<DownmixGain> gains(channels);
  size_t channel = 0;
  for (uint32_t bit = 1; bit != 0 && channel < channels; bit <<= 1) {
    if (mask & bit) {
      gains[channel++] = GainOf(bit);
    }
  }
  return gains;
}

inline int16_t ClipToInt16(float v) {
  return static_cast<int16_t>(std::lround(std::clamp(v, -32768.0f, 32767.0f)));
}

// Mixes one interleaved frame, `gains.size()` samples long, down to two.
inline void DownmixFrameToStereo(const int16_t* frame,
                                 const std::vector<DownmixGain>& gains,
                                 int16_t* left,
                                 int16_t* right) {
  float l = 0.0f;
  float r = 0.0f;
  for (size_t c = 0; c < gains.size(); ++c) {
    l += gains[c].left * frame[c];
    r += gains[c].right * frame[c];
  }
  *left = ClipToInt16(l);
  *right = ClipToInt16(r);
}

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_LOOPBACK_DOWNMIX_H_
