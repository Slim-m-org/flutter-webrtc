#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#include "check.h"
#include "flutter_frame_fit.h"
#include "frame_fit_reference.h"

namespace {

using flutter_webrtc_plugin::FitWithin;
using flutter_webrtc_plugin::FrameFitter;
using flutter_webrtc_plugin::FrameSize;
using flutter_webrtc_plugin::I420View;
using flutter_webrtc_plugin::RequestedFrameSize;
using flutter_webrtc_plugin::ScaledI420;

// A frame whose left half is one colour and right half another, tightly packed.
struct Frame {
  Frame(int w, int h, uint8_t left, uint8_t right)
      : width(w), height(h), y(w * h), u(((w + 1) / 2) * ((h + 1) / 2), 100),
        v(u.size(), 150) {
    for (int row = 0; row < h; ++row) {
      for (int col = 0; col < w; ++col) {
        y[row * w + col] = col < w / 2 ? left : right;
      }
    }
  }

  I420View View() const {
    const int uv = (width + 1) / 2;
    return {width, height, y.data(), width, u.data(), uv, v.data(), uv};
  }

  int width, height;
  std::vector<uint8_t> y, u, v;
};

TEST(ShareFit_FourKOnA1080pRequestComesOutAt1080p) {
  Frame in(3840, 2160, 40, 200);
  FrameFitter fitter({1920, 1080});
  const ScaledI420* out = fitter.Fit(in.View());
  CHECK(out != nullptr);
  if (!out) return;
  CHECK(out->width == 1920);
  CHECK(out->height == 1080);
  CHECK(out->y.size() == 1920u * 1080u);
  CHECK(out->u.size() == 960u * 540u);
}

TEST(ShareFit_ScaledPixelsKeepTheirColour) {
  Frame in(3840, 2160, 40, 200);
  FrameFitter fitter({1920, 1080});
  const ScaledI420* out = fitter.Fit(in.View());
  CHECK(out != nullptr);
  if (!out) return;
  CHECK(out->y[0] == 40);
  CHECK(out->y[1079 * 1920 + 1919] == 200);
  CHECK(out->u[0] == 100);
  CHECK(out->v[539 * 960 + 959] == 150);
}

TEST(ShareFit_ASourceWithinTheRequestPassesThrough) {
  Frame exact(1920, 1080, 10, 20);
  Frame smaller(1280, 720, 10, 20);
  FrameFitter fitter({1920, 1080});
  CHECK(fitter.Fit(exact.View()) == nullptr);
  CHECK(fitter.Fit(smaller.View()) == nullptr);
}

TEST(ShareFit_UltrawideKeepsItsAspectRatio) {
  CHECK(FitWithin({3440, 1440}, {1920, 1080}) == (FrameSize{1920, 804}));
  Frame in(3440, 1440, 40, 200);
  FrameFitter fitter({1920, 1080});
  const ScaledI420* out = fitter.Fit(in.View());
  CHECK(out != nullptr);
  if (!out) return;
  CHECK(out->width == 1920);
  CHECK(out->height == 804);
}

TEST(ShareFit_SuperUltrawideIsBoundByWidthAndStaysEven) {
  const FrameSize fitted = FitWithin({5120, 1080}, {1920, 1080});
  CHECK(fitted.width == 1920);
  CHECK(fitted.height == 406);
}

TEST(ShareFit_ATallerScreenIsBoundByHeight) {
  CHECK(FitWithin({2560, 1600}, {2560, 1440}) == (FrameSize{2304, 1440}));
  CHECK(FitWithin({1080, 1920}, {1920, 1080}) == (FrameSize{608, 1080}));
}

TEST(ShareFit_NeverUpscalesOrReturnsOddSides) {
  CHECK(FitWithin({1280, 720}, {3840, 2160}) == (FrameSize{1280, 720}));
  const FrameSize fitted = FitWithin({3841, 2161}, {1921, 1081});
  CHECK(fitted.width % 2 == 0 && fitted.height % 2 == 0);
  CHECK(fitted.width <= 1921 && fitted.height <= 1081);
}

TEST(ShareFit_AnUnboundedSideLeavesThatSideFree) {
  CHECK(FitWithin({3840, 2160}, {1920, 0}) == (FrameSize{1920, 1080}));
  CHECK(FitWithin({3840, 2160}, {0, 1080}) == (FrameSize{1920, 1080}));
  CHECK(FitWithin({3840, 2160}, {0, 0}) == (FrameSize{3840, 2160}));
}

TEST(ShareFit_OddSourceDimensionsScaleWithoutOverrun) {
  Frame in(2561, 1441, 40, 200);
  FrameFitter fitter({1920, 1080});
  const ScaledI420* out = fitter.Fit(in.View());
  CHECK(out != nullptr);
  if (!out) return;
  CHECK(out->width % 2 == 0 && out->height % 2 == 0);
  CHECK(out->width <= 1920 && out->height <= 1080);
  CHECK(out->y[0] == 40);
}

TEST(ShareFit_TheOutputBufferIsReusedAcrossFrames) {
  Frame in(3840, 2160, 40, 200);
  FrameFitter fitter({1920, 1080});
  const ScaledI420* first = fitter.Fit(in.View());
  CHECK(first != nullptr);
  if (!first) return;
  const uint8_t* data = first->y.data();
  const ScaledI420* second = fitter.Fit(in.View());
  CHECK(second == first);
  CHECK(second->y.data() == data);
}

// Screen-like content: noise, so every tap of every output counts.
struct NoiseFrame {
  NoiseFrame(int w, int h, uint32_t seed)
      : width(w),
        height(h),
        y(static_cast<size_t>(w) * h),
        u(static_cast<size_t>((w + 1) / 2) * ((h + 1) / 2)),
        v(u.size()) {
    std::mt19937 rng(seed);
    for (auto* plane : {&y, &u, &v}) {
      for (auto& p : *plane)
        p = static_cast<uint8_t>(rng());
    }
  }

  I420View View() const {
    const int uv = (width + 1) / 2;
    return {width, height, y.data(), width, u.data(), uv, v.data(), uv};
  }

  int width, height;
  std::vector<uint8_t> y, u, v;
};

int MaxDifference(const std::vector<uint8_t>& a,
                  const std::vector<uint8_t>& b) {
  if (a.size() != b.size())
    return 256;
  int worst = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    worst = std::max(worst, std::abs(int{a[i]} - int{b[i]}));
  }
  return worst;
}

// The scaled frame against the first scaler's, which the fps readback of
// 2026-11-02 shipped: same size, and no sample more than one level apart.
void CheckMatchesFirstScaler(const I420View& in, const ScaledI420& out) {
  ScaledI420 first;
  first.width = out.width;
  first.height = out.height;
  frame_fit_reference::Scale(in, &first);
  CHECK(out.y.size() == first.y.size());
  CHECK(out.u.size() == first.u.size());
  CHECK(MaxDifference(out.y, first.y) <= 1);
  CHECK(MaxDifference(out.u, first.u) <= 1);
  CHECK(MaxDifference(out.v, first.v) <= 1);
}

TEST(ShareFit_TheShippedScreensComeOutAtTheirTierSize) {
  struct Case {
    FrameSize source, tier, out;
  };
  const Case cases[] = {{{3840, 2160}, {1920, 1080}, {1920, 1080}},
                        {{3440, 1440}, {1920, 1080}, {1920, 804}},
                        {{2560, 1600}, {2560, 1440}, {2304, 1440}},
                        {{5120, 1080}, {1920, 1080}, {1920, 406}}};
  for (const Case& c : cases) {
    const NoiseFrame in(c.source.width, c.source.height, 1);
    FrameFitter fitter(c.tier);
    const ScaledI420* out = fitter.Fit(in.View());
    CHECK(out != nullptr);
    if (!out)
      continue;
    CHECK(out->width == c.out.width && out->height == c.out.height);
    CHECK(out->y.size() == static_cast<size_t>(c.out.width) * c.out.height);
    CHECK(out->u.size() ==
          static_cast<size_t>(c.out.width / 2) * (c.out.height / 2));
    CheckMatchesFirstScaler(in.View(), *out);
  }
}

TEST(ShareFit_OddAndSteepScalesMatchTheFirstScaler) {
  const FrameSize sources[] = {{2561, 1441}, {3840, 2160}, {5120, 1080}};
  const FrameSize tiers[] = {{1920, 1080}, {640, 360}, {1280, 720}};
  for (const FrameSize source : sources) {
    for (const FrameSize tier : tiers) {
      const NoiseFrame in(source.width, source.height, 2);
      FrameFitter fitter(tier);
      const ScaledI420* out = fitter.Fit(in.View());
      CHECK(out != nullptr);
      if (out)
        CheckMatchesFirstScaler(in.View(), *out);
    }
  }
}

TEST(ShareFit_ASourceThatChangesSizeMidStreamGetsFreshTaps) {
  FrameFitter fitter({1920, 1080});
  const NoiseFrame sizes[] = {
      {3840, 2160, 3}, {2560, 1440, 4}, {3440, 1440, 5}, {3840, 2160, 6}};
  for (const NoiseFrame& in : sizes) {
    const ScaledI420* out = fitter.Fit(in.View());
    CHECK(out != nullptr);
    if (out)
      CheckMatchesFirstScaler(in.View(), *out);
  }
}

TEST(ShareFit_ReadsTheSizeFromGetDisplayMediaConstraints) {
  using flutter::EncodableMap;
  using flutter::EncodableValue;
  EncodableMap video;
  CHECK(!RequestedFrameSize(video).has_value());
  video[EncodableValue("width")] = EncodableValue(1920);
  video[EncodableValue("height")] = EncodableValue(int64_t{1080});
  CHECK(RequestedFrameSize(video) == (FrameSize{1920, 1080}));

  EncodableMap ideal;
  ideal[EncodableValue("ideal")] = EncodableValue(1280.0);
  EncodableMap by_map;
  by_map[EncodableValue("width")] = EncodableValue(ideal);
  CHECK(RequestedFrameSize(by_map) == (FrameSize{1280, 0}));

  EncodableMap bad;
  bad[EncodableValue("width")] = EncodableValue(-5);
  bad[EncodableValue("height")] = EncodableValue(std::string("big"));
  CHECK(!RequestedFrameSize(bad).has_value());
}

}  // namespace
