#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_CHAIN_GLUE_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_CHAIN_GLUE_H_

#include <cstddef>
#include <cstdint>
#include <limits>

// The length and format rules of the Android JNI and iOS Objective-C++ shims,
// kept free of platform headers so test/native runs them on any host.
namespace flutter_webrtc_plugin {

constexpr float kDefaultGateThresholdDb = -50.0f;

// A direct float buffer holds a frame only when it exists and has room for every sample.
inline bool FrameFitsBuffer(const void* samples, int64_t capacity_bytes,
                            int num_frames) {
  if (samples == nullptr || num_frames <= 0 || capacity_bytes < 0) return false;
  return capacity_bytes >= static_cast<int64_t>(num_frames) * 4;
}

// An audio buffer is processed only with a channel and at least one sample in it.
inline bool AudioBufferUsable(size_t channels, size_t frames) {
  return channels != 0 && frames != 0 &&
         frames <= static_cast<size_t>(std::numeric_limits<int>::max());
}

// A size from the platform narrowed to the int the chain takes, saturating instead of wrapping.
inline int ClampToInt(size_t value) {
  constexpr size_t kMax = static_cast<size_t>(std::numeric_limits<int>::max());
  return static_cast<int>(value > kMax ? kMax : value);
}

// The threshold a settings map carries, or the default when it names none.
inline float GateThresholdOrDefault(bool present, float value) {
  return present ? value : kDefaultGateThresholdDb;
}

// One bit per enabled stage, so a settings change logs only when the stages change.
inline int ChainStageMask(bool gate, bool strong, bool dynamics,
                          bool leveler) {
  return (gate ? 1 : 0) | (strong ? 2 : 0) | (dynamics ? 4 : 0) |
         (leveler ? 8 : 0);
}

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_CHAIN_GLUE_H_
