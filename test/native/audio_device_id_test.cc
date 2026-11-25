#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "flutter_audio_device_id.h"

namespace {

using flutter_webrtc_plugin::AudioDeviceIdAt;
using flutter_webrtc_plugin::AudioDeviceIdFromBuffers;

struct Entry {
  const char* name;
  const char* guid;
};

// Names devices the way the ADM does: into the caller's buffers.
struct FakeAudioDevice {
  std::vector<Entry> capture;
  std::vector<Entry> playout;

  static void Fill(const Entry& e, char* name, char* guid) {
    std::strcpy(name, e.name);
    std::strcpy(guid, e.guid);
  }
  int32_t RecordingDeviceName(uint16_t i, char* name, char* guid) {
    Fill(capture.at(i), name, guid);
    return 0;
  }
  int32_t PlayoutDeviceName(uint16_t i, char* name, char* guid) {
    Fill(playout.at(i), name, guid);
    return 0;
  }
};

std::vector<std::string> Listed(FakeAudioDevice& d, bool capture) {
  std::vector<std::string> ids;
  const auto& entries = capture ? d.capture : d.playout;
  for (const Entry& e : entries) ids.push_back(AudioDeviceIdFromBuffers(e.name, e.guid));
  return ids;
}

}  // namespace

TEST(AudioDeviceId_TheDefaultsIdIsOneTheDeviceListShows) {
  FakeAudioDevice device;
  device.capture = {{"Headset", "{0.0.1}.{aaa}"}, {"Webcam mic", ""}};
  device.playout = {{"Speakers", "{0.0.0}.{bbb}"}, {"Monitor", "{0.0.0}.{ccc}"}};
  for (const bool capture : {true, false}) {
    const std::vector<std::string> listed = Listed(device, capture);
    for (uint16_t i = 0; i < listed.size(); ++i) {
      CHECK(AudioDeviceIdAt(device, capture, i) == listed[i]);
    }
  }
}

TEST(AudioDeviceId_ADeviceWithNoGuidIsKnownByName) {
  FakeAudioDevice device;
  device.capture = {{"Webcam mic", ""}};
  CHECK(AudioDeviceIdAt(device, true, 0) == "Webcam mic");
}
