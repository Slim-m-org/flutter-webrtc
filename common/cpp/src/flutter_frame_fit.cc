#include "flutter_frame_fit.h"

#include <algorithm>
#include <cmath>

namespace flutter_webrtc_plugin {

namespace {

constexpr int kWeightBits = 16;
constexpr uint32_t kWeightOne = 1u << kWeightBits;

// One output sample's share of each source sample under it.
struct Taps {
  int first = 0;
  std::vector<uint32_t> weights;
};

// Every output sample covers src/dst source samples; a source sample on the
// edge is shared by the weight of the part that lies inside. The weights of a
// sample add up to exactly kWeightOne, so flat areas keep their value.
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
    // Rounding leaves the sum off by a few units; the heaviest tap absorbs it.
    auto heaviest = std::max_element(t.weights.begin(), t.weights.end());
    *heaviest = static_cast<uint32_t>(*heaviest + kWeightOne - total);
  }
  return taps;
}

// Area-averaging downscale of one plane: columns first into `scratch` with
// eight fractional bits, then rows.
void ScalePlane(const uint8_t* src, int src_stride, int src_w, int src_h,
                uint8_t* dst, int dst_stride, int dst_w, int dst_h,
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

// Reads a side of a constraint: a bare number, or a map with exact, ideal or
// max in that order of preference.
int SideOf(const flutter::EncodableMap& video, const char* key) {
  const auto found = video.find(flutter::EncodableValue(key));
  if (found == video.end()) return 0;
  const flutter::EncodableValue& value = found->second;
  if (const auto* i = std::get_if<int32_t>(&value)) return std::max(*i, 0);
  if (const auto* l = std::get_if<int64_t>(&value)) {
    return static_cast<int>(std::clamp<int64_t>(*l, 0, 1 << 20));
  }
  if (const auto* d = std::get_if<double>(&value)) {
    return static_cast<int>(std::clamp(*d, 0.0, 1048576.0));
  }
  if (const auto* m = std::get_if<flutter::EncodableMap>(&value)) {
    for (const char* name : {"exact", "ideal", "max"}) {
      if (const int side = SideOf(*m, name); side > 0) return side;
    }
  }
  return 0;
}

int EvenAtLeastTwo(int v) { return std::max(2, v & ~1); }

int NearestEven(double v) { return 2 * static_cast<int>(std::lround(v / 2)); }

}  // namespace

std::optional<FrameSize> RequestedFrameSize(
    const flutter::EncodableMap& video) {
  const FrameSize size{SideOf(video, "width"), SideOf(video, "height")};
  if (size.width == 0 && size.height == 0) return std::nullopt;
  return size;
}

FrameSize FitWithin(FrameSize source, FrameSize limit) {
  const bool fits_width = limit.width <= 0 || source.width <= limit.width;
  const bool fits_height = limit.height <= 0 || source.height <= limit.height;
  if (fits_width && fits_height) return source;

  // The side that runs out first decides, compared without division.
  const bool width_decides =
      limit.width > 0 &&
      (limit.height <= 0 ||
       static_cast<int64_t>(limit.width) * source.height <=
           static_cast<int64_t>(limit.height) * source.width);
  FrameSize fitted;
  if (width_decides) {
    fitted.width = EvenAtLeastTwo(limit.width);
    fitted.height = NearestEven(static_cast<double>(source.height) *
                                fitted.width / source.width);
    if (limit.height > 0) {
      fitted.height = std::min(fitted.height, EvenAtLeastTwo(limit.height));
    }
  } else {
    fitted.height = EvenAtLeastTwo(limit.height);
    fitted.width = NearestEven(static_cast<double>(source.width) *
                               fitted.height / source.height);
    if (limit.width > 0) {
      fitted.width = std::min(fitted.width, EvenAtLeastTwo(limit.width));
    }
  }
  fitted.width = std::max(2, fitted.width);
  fitted.height = std::max(2, fitted.height);
  return fitted;
}

const ScaledI420* FrameFitter::Fit(const I420View& in) {
  if (in.width <= 0 || in.height <= 0) return nullptr;
  const FrameSize target = FitWithin({in.width, in.height}, limit_);
  if (target.width >= in.width && target.height >= in.height) return nullptr;

  out_.width = target.width;
  out_.height = target.height;
  const int chroma_w = out_.stride_uv();
  const int chroma_h = (target.height + 1) / 2;
  out_.y.resize(static_cast<size_t>(target.width) * target.height);
  out_.u.resize(static_cast<size_t>(chroma_w) * chroma_h);
  out_.v.resize(out_.u.size());

  const int src_chroma_w = (in.width + 1) / 2;
  const int src_chroma_h = (in.height + 1) / 2;
  ScalePlane(in.y, in.stride_y, in.width, in.height, out_.y.data(),
             out_.stride_y(), target.width, target.height, &scratch_);
  ScalePlane(in.u, in.stride_u, src_chroma_w, src_chroma_h, out_.u.data(),
             chroma_w, chroma_w, chroma_h, &scratch_);
  ScalePlane(in.v, in.stride_v, src_chroma_w, src_chroma_h, out_.v.data(),
             chroma_w, chroma_w, chroma_h, &scratch_);
  return &out_;
}

}  // namespace flutter_webrtc_plugin
