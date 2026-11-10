#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "check.h"
#include "locked_map.h"

namespace {

using flutter_webrtc_plugin::LockedMap;
using Tracks = LockedMap<std::string, std::shared_ptr<int>>;

TEST(LockedMap_FindReturnsWhatWasSet) {
  Tracks tracks;
  tracks.Set("a", std::make_shared<int>(1));
  tracks.Set("b", std::make_shared<int>(2));
  CHECK(tracks.Find("a") && *tracks.Find("a") == 1);
  CHECK(tracks.Find("b") && *tracks.Find("b") == 2);
  tracks.Set("a", std::make_shared<int>(3));
  CHECK(*tracks.Find("a") == 3);
}

TEST(LockedMap_FindOfAnAbsentKeyIsNull) {
  Tracks tracks;
  CHECK(tracks.Find("missing") == nullptr);
  tracks.Set("a", std::make_shared<int>(1));
  tracks.Erase("a");
  CHECK(tracks.Find("a") == nullptr);
  tracks.Erase("never-added");
}

TEST(LockedMap_ValuesIsASnapshotTheMapCannotChange) {
  Tracks tracks;
  tracks.Set("a", std::make_shared<int>(1));
  tracks.Set("b", std::make_shared<int>(2));
  auto values = tracks.Values();
  tracks.Erase("a");
  tracks.Set("c", std::make_shared<int>(3));
  CHECK(values.size() == 2);
  CHECK(tracks.Values().size() == 2);
}

// A writer on one thread and a reader on another, the shape of a signaling
// thread adding tracks while the platform thread looks one up. Meaningful
// under -fsanitize=thread, where a missing lock is reported as a race.
TEST(LockedMap_SurvivesAWriterAndReaders) {
  Tracks tracks;
  std::atomic<bool> done{false};
  std::atomic<long> reads{0};
  std::thread writer([&] {
    for (int i = 0; i < 20000; ++i) {
      const std::string id = "t" + std::to_string(i % 64);
      tracks.Set(id, std::make_shared<int>(i));
      if (i % 3 == 0) {
        tracks.Erase(id);
      }
    }
    done = true;
  });
  std::thread finder([&] {
    while (!done) {
      auto found = tracks.Find("t7");
      reads += found ? 1 : 0;
      (void)tracks.Values();
    }
  });
  writer.join();
  finder.join();
  CHECK(done.load());
}

}  // namespace
