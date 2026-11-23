#include "frame_fit_reference.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace frame_fit_reference {

namespace {

constexpr int kWeightBits = 16;
constexpr uint32_t kWeightOne = 1u << kWeightBits;

struct Taps {
  int first = 0;
  std::vector<uint32_t> weights;
};

std::vector<Taps> BuildTaps(int src, int dst) {
  std::vector<Taps> taps(dst);
  for (int d = 0; d < dst; ++d) {
    const double begin = static_cast<double>(d) * src / dst;
    const double end = static_cast<double>(d + 1) * src / dst;
    const int first = static_cast<int>(std::floor(begin));
    const int last = std::min(src, static_cast<int>(std::ceil(end)));
    Taps& t = taps[d];
    t.first = first;
    uint32_t total = 0;
    for (int s = first; s < last; ++s) {
      const double overlap =
          std::min<double>(end, s + 1) - std::max<double>(begin, s);
      const auto w = static_cast<uint32_t>(
          std::lround(overlap / (end - begin) * kWeightOne));
      t.weights.push_back(w);
      total += w;
    }
    auto heaviest = std::max_element(t.weights.begin(), t.weights.end());
    *heaviest = static_cast<uint32_t>(*heaviest + kWeightOne - total);
  }
  return taps;
}

void ScalePlane(const uint8_t* src,
                int src_stride,
                int src_w,
                int src_h,
                uint8_t* dst,
                int dst_stride,
                int dst_w,
                int dst_h,
                std::vector<uint16_t>* scratch) {
  const std::vector<Taps> cols = BuildTaps(src_w, dst_w);
  const std::vector<Taps> rows = BuildTaps(src_h, dst_h);
  scratch->resize(static_cast<size_t>(src_h) * dst_w);

  for (int y = 0; y < src_h; ++y) {
    const uint8_t* line = src + static_cast<size_t>(y) * src_stride;
    uint16_t* mid = scratch->data() + static_cast<size_t>(y) * dst_w;
    for (int x = 0; x < dst_w; ++x) {
      const Taps& t = cols[x];
      uint32_t sum = 0;
      for (size_t i = 0; i < t.weights.size(); ++i) {
        sum += t.weights[i] * line[t.first + i];
      }
      mid[x] = static_cast<uint16_t>(sum >> (kWeightBits - 8));
    }
  }

  for (int y = 0; y < dst_h; ++y) {
    const Taps& t = rows[y];
    uint8_t* line = dst + static_cast<size_t>(y) * dst_stride;
    for (int x = 0; x < dst_w; ++x) {
      uint64_t sum = 0;
      for (size_t i = 0; i < t.weights.size(); ++i) {
        sum += static_cast<uint64_t>(t.weights[i]) *
               (*scratch)[static_cast<size_t>(t.first + i) * dst_w + x];
      }
      line[x] = static_cast<uint8_t>((sum + (1u << (kWeightBits + 7))) >>
                                     (kWeightBits + 8));
    }
  }
}

}  // namespace

void Scale(const flutter_webrtc_plugin::I420View& in,
           flutter_webrtc_plugin::ScaledI420* out) {
  const int chroma_w = out->stride_uv();
  const int chroma_h = (out->height + 1) / 2;
  out->y.resize(static_cast<size_t>(out->width) * out->height);
  out->u.resize(static_cast<size_t>(chroma_w) * chroma_h);
  out->v.resize(out->u.size());
  const int src_chroma_w = (in.width + 1) / 2;
  const int src_chroma_h = (in.height + 1) / 2;
  std::vector<uint16_t> scratch;
  ScalePlane(in.y, in.stride_y, in.width, in.height, out->y.data(),
             out->stride_y(), out->width, out->height, &scratch);
  ScalePlane(in.u, in.stride_u, src_chroma_w, src_chroma_h, out->u.data(),
             chroma_w, chroma_w, chroma_h, &scratch);
  ScalePlane(in.v, in.stride_v, src_chroma_w, src_chroma_h, out->v.data(),
             chroma_w, chroma_w, chroma_h, &scratch);
}

}  // namespace frame_fit_reference
