#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_AUDIO_DEVICE_ID_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_AUDIO_DEVICE_ID_H_

#include <cstdint>
#include <cstring>
#include <string>

#include "flutter_utf8_sanitize.h"

namespace flutter_webrtc_plugin {

// The id Dart sees for a device: its guid, or its name when it has none.
inline std::string AudioDeviceIdFromBuffers(const char* name, const char* guid) {
  const std::string raw = (guid != nullptr && std::strlen(guid) > 0)
                              ? std::string(guid)
                              : std::string(name != nullptr ? name : "");
  return SanitizeUtf8ForFlutter(raw);
}

// The id of the device at [index], the same one the device list gives it.
template <typename Device>
std::string AudioDeviceIdAt(Device& device, bool capture, uint16_t index) {
  char name[256] = {0};
  char guid[256] = {0};
  if (capture) {
    device.RecordingDeviceName(index, name, guid);
  } else {
    device.PlayoutDeviceName(index, name, guid);
  }
  return AudioDeviceIdFromBuffers(name, guid);
}

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_AUDIO_DEVICE_ID_H_
