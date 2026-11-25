#include <string>
#include <vector>

#include "audio_device_lookup.h"
#include "check.h"

namespace {

using flutter_webrtc_plugin::IndexOfDeviceGuid;

int Find(const std::vector<std::string>& guids, const std::string& guid) {
  return IndexOfDeviceGuid(static_cast<int>(guids.size()), guid,
                           [&](int index, std::string* out) {
                             *out = guids[index];
                             return true;
                           });
}

TEST(AudioDeviceLookup_FindsTheDeviceWhereItIsListedNow) {
  CHECK(Find({"a", "b", "c"}, "b") == 1);
  CHECK(Find({"x", "a", "b", "c"}, "b") == 2);
}

TEST(AudioDeviceLookup_AnUnpluggedDeviceIsNotFound) {
  CHECK(Find({"a", "c"}, "b") == -1);
  CHECK(Find({}, "b") == -1);
}

TEST(AudioDeviceLookup_AnEmptyGuidMatchesNothing) {
  CHECK(Find({"", "a"}, "") == -1);
}

TEST(AudioDeviceLookup_AnUnreadableEntryIsSkipped) {
  const int index = IndexOfDeviceGuid(
      3, "c", [](int i, std::string* out) {
        if (i == 0) return false;
        *out = i == 2 ? "c" : "b";
        return true;
      });
  CHECK(index == 2);
}

}  // namespace
