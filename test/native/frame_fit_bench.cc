#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

#include "flutter_frame_fit.h"
#include "frame_fit_reference.h"

using flutter_webrtc_plugin::FitWithin;
using flutter_webrtc_plugin::FrameFitter;
using flutter_webrtc_plugin::FrameSize;
using flutter_webrtc_plugin::I420View;
using flutter_webrtc_plugin::ScaledI420;

namespace {

struct Case {
  FrameSize source;
  FrameSize tier;
};

struct Planes {
  explicit Planes(FrameSize s)
      : size(s),
        y(static_cast<size_t>(s.width) * s.height),
        u(static_cast<size_t>((s.width + 1) / 2) * ((s.height + 1) / 2)),
        v(u.size()) {
    std::mt19937 rng(7);
    for (auto* plane : {&y, &u, &v}) {
      for (auto& p : *plane)
        p = static_cast<uint8_t>(rng());
    }
  }

  I420View View() const {
    const int uv = (size.width + 1) / 2;
    return {size.width, size.height, y.data(), size.width,
            u.data(),   uv,          v.data(), uv};
  }

  FrameSize size;
  std::vector<uint8_t> y, u, v;
};

template <typename F>
double MedianMs(int frames, F&& body) {
  std::vector<double> ms;
  for (int i = 0; i < frames; ++i) {
    const auto start = std::chrono::steady_clock::now();
    body();
    const auto end = std::chrono::steady_clock::now();
    ms.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
  }
  std::sort(ms.begin(), ms.end());
  return ms[ms.size() / 2];
}

// What RTCVideoFrame::Create costs on top of the scale: a fresh buffer and a
// copy of the planes.
void CopyLikeCreate(const ScaledI420& s) {
  const size_t total = s.y.size() + s.u.size() + s.v.size();
  std::unique_ptr<uint8_t[]> frame(new uint8_t[total]);
  std::memcpy(frame.get(), s.y.data(), s.y.size());
  std::memcpy(frame.get() + s.y.size(), s.u.data(), s.u.size());
  std::memcpy(frame.get() + s.y.size() + s.u.size(), s.v.data(), s.v.size());
  volatile uint8_t sink = frame[total - 1];
  (void)sink;
}

}  // namespace

// usage: frame_fit_bench [frames]; prints the median ms per frame of the
// shipped scaler, the first scaler it replaced, and the frame copy that follows
// either.
int main(int argc, char** argv) {
  const int frames = argc > 1 ? std::max(1, std::atoi(argv[1])) : 60;
  const Case cases[] = {{{3840, 2160}, {1920, 1080}},
                        {{3440, 1440}, {1920, 1080}},
                        {{2560, 1600}, {2560, 1440}},
                        {{5120, 1080}, {1920, 1080}}};
  std::printf("%-11s %-11s %9s %9s %9s\n", "source", "target", "fitter",
              "first", "copy");
  for (const Case& c : cases) {
    const Planes in(c.source);
    const FrameSize target = FitWithin(c.source, c.tier);
    FrameFitter fitter(c.tier);
    const ScaledI420* out = fitter.Fit(in.View());
    const double fitter_ms =
        MedianMs(frames, [&] { out = fitter.Fit(in.View()); });
    ScaledI420 first;
    first.width = target.width;
    first.height = target.height;
    const double first_ms = MedianMs(
        frames, [&] { frame_fit_reference::Scale(in.View(), &first); });
    const double copy_ms = MedianMs(frames, [&] { CopyLikeCreate(*out); });
    std::printf("%5dx%-5d %5dx%-5d %9.2f %9.2f %9.2f\n", c.source.width,
                c.source.height, target.width, target.height, fitter_ms,
                first_ms, copy_ms);
  }
  return 0;
}
