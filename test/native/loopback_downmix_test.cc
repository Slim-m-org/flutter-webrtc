#include <cstdint>
#include <vector>

#include "check.h"
#include "loopback_downmix.h"

namespace {

using flutter_webrtc_plugin::DownmixFrameToStereo;
using flutter_webrtc_plugin::StereoDownmixGains;

constexpr uint32_t k51 = 0x3F;
constexpr uint32_t k71 = 0x63F;

struct Stereo {
  int16_t left;
  int16_t right;
};

Stereo Mix(size_t channels, uint32_t mask, const std::vector<int16_t>& frame) {
  Stereo out{};
  DownmixFrameToStereo(frame.data(), StereoDownmixGains(channels, mask),
                       &out.left, &out.right);
  return out;
}

// Order of a 5.1 frame: FL FR FC LFE BL BR.
TEST(LoopbackDownmix_CentreOnlyFiveOneIsAudibleOnBothSides) {
  const Stereo out = Mix(6, k51, {0, 0, 10000, 0, 0, 0});
  CHECK(out.left > 6000 && out.left < 8000);
  CHECK(out.left == out.right);
}

TEST(LoopbackDownmix_FrontPairPassesThroughAtFullLevel) {
  const Stereo out = Mix(6, k51, {1000, -2000, 0, 0, 0, 0});
  CHECK(out.left == 1000);
  CHECK(out.right == -2000);
}

TEST(LoopbackDownmix_LfeIsDropped) {
  const Stereo out = Mix(6, k51, {0, 0, 0, 30000, 0, 0});
  CHECK(out.left == 0 && out.right == 0);
}

TEST(LoopbackDownmix_SurroundsGoToTheirOwnSideOnly) {
  const Stereo out = Mix(6, k51, {0, 0, 0, 0, 10000, 0});
  CHECK(out.left > 6000 && out.right == 0);
}

// Order of a 7.1 frame: FL FR FC LFE BL BR SL SR.
TEST(LoopbackDownmix_SevenOneSidesGoToTheirOwnSide) {
  const Stereo out = Mix(8, k71, {0, 0, 0, 0, 0, 0, 0, 10000});
  CHECK(out.left == 0 && out.right > 6000);
}

TEST(LoopbackDownmix_SumsAreClippedNotWrapped) {
  const Stereo out = Mix(6, k51, {30000, 0, 30000, 0, 30000, 0});
  CHECK(out.left == 32767);
}

TEST(LoopbackDownmix_MissingMaskFallsBackToTheStandardLayout) {
  const Stereo out = Mix(6, 0, {0, 0, 10000, 0, 0, 0});
  CHECK(out.left > 6000 && out.left == out.right);
}

TEST(LoopbackDownmix_MonoIsCopiedToBothSides) {
  const Stereo out = Mix(1, 0, {1234});
  CHECK(out.left == 1234 && out.right == 1234);
}

TEST(LoopbackDownmix_StereoIsUnchanged) {
  const Stereo out = Mix(2, 0x3, {-500, 700});
  CHECK(out.left == -500 && out.right == 700);
}

}  // namespace
