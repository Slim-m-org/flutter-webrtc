#ifndef FLUTTER_WEBRTC_PLUGIN_AUDIO_DEVICE_LOOKUP_H_
#define FLUTTER_WEBRTC_PLUGIN_AUDIO_DEVICE_LOOKUP_H_

#include <string>

namespace flutter_webrtc_plugin {

// The list index whose GUID is `guid` right now, or -1 when it is not listed.
// `guid_at(index, out)` fills the GUID of one entry and returns whether it could.
template <typename GuidAt>
int IndexOfDeviceGuid(int count, const std::string& guid, GuidAt guid_at) {
  if (guid.empty()) return -1;
  for (int i = 0; i < count; i++) {
    std::string current;
    if (guid_at(i, &current) && current == guid) return i;
  }
  return -1;
}

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_AUDIO_DEVICE_LOOKUP_H_
