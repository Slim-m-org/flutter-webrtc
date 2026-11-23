#include "flutter_frame_fit.h"

#include <algorithm>
#include <cmath>

namespace flutter_webrtc_plugin {

namespace {

constexpr int kWeightBits = 14;
constexpr uint32_t kWeightOne = 1u << kWeightBits;
// The row pass keeps eight fractional bits, so its sums fit in 16 bits.
constexpr int kRowShift = kWeightBits - 8;
constexpr int kColShift = kWeightBits + 8;

// The column pass of one row; a fixed tap count lets the compiler unroll the
// taps, and kTaps 0 reads the count at run time.
template <int kTaps>
void ScaleLine(const uint16_t* mid, const AxisTaps& cols, uint8_t* out) {
  const int taps = kTaps > 0 ? kTaps : cols.count;
  const uint16_t* wx = cols.weights.data();
  for (int x = 0; x < cols.dst; ++x, wx += taps) {
    const uint16_t* in = mid + cols.first[x];
    uint32_t acc = 1u << (kColShift - 1);
    for (int j = 0; j < taps; ++j)
      acc += uint32_t{wx[j]} * in[j];
    out[x] = static_cast<uint8_t>(acc >> kColShift);
  }
}

// Halving gives every output two taps of exactly half weight, so this is the
// generic column pass's result, in a loop the compiler can vectorize.
void HalveLine(const uint16_t* mid, int dst_w, uint8_t* out) {
  constexpr int kHalfShift = kColShift - kWeightBits + 1;
  for (int x = 0; x < dst_w; ++x) {
    const uint32_t pair = uint32_t{mid[2 * x]} + mid[2 * x + 1];
    out[x] =
        static_cast<uint8_t>((pair + (1u << (kHalfShift - 1))) >> kHalfShift);
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

// Every output sample covers src/dst source samples; a source sample on the
// edge is shared by the weight of the part that lies inside. The weights of a
// sample add up to exactly kWeightOne, so flat areas keep their value.
void AxisTaps::Prepare(int src_samples, int dst_samples) {
  if (src == src_samples && dst == dst_samples)
    return;
  src = src_samples;
  dst = dst_samples;
  std::vector<double> begins(dst + 1);
  for (int d = 0; d <= dst; ++d)
    begins[d] = static_cast<double>(d) * src / dst;
  count = 1;
  for (int d = 0; d < dst; ++d) {
    const int lo = static_cast<int>(std::floor(begins[d]));
    const int hi = std::min(src, static_cast<int>(std::ceil(begins[d + 1])));
    count = std::max(count, hi - lo);
  }
  first.assign(dst, 0);
  weights.assign(static_cast<size_t>(dst) * count, 0);
  for (int d = 0; d < dst; ++d) {
    const double begin = begins[d];
    const double end = begins[d + 1];
    const int lo = static_cast<int>(std::floor(begin));
    const int hi = std::min(src, static_cast<int>(std::ceil(end)));
    // The last outputs start early enough that their padded taps stay inside.
    first[d] = std::min(lo, src - count);
    uint16_t* w = weights.data() + static_cast<size_t>(d) * count;
    uint16_t* heaviest = w + (lo - first[d]);
    uint32_t total = 0;
    for (int s = lo; s < hi; ++s) {
      const double overlap =
          std::min<double>(end, s + 1) - std::max<double>(begin, s);
      uint16_t& tap = w[s - first[d]];
      tap = static_cast<uint16_t>(
          std::lround(overlap / (end - begin) * kWeightOne));
      total += tap;
      if (tap > *heaviest)
        heaviest = &tap;
    }
    // Rounding leaves the sum off by a few units; the heaviest tap absorbs it.
    *heaviest = static_cast<uint16_t>(*heaviest + kWeightOne - total);
  }
}

// Rows first, so the column pass runs once per output row rather than once per
// source row, and the row pass is a straight multiply-add along each line.
void FrameFitter::ScalePlane(const uint8_t* src,
                             int src_stride,
                             const PlaneTaps& taps,
                             uint8_t* dst,
                             int dst_stride) {
  const AxisTaps& rows = taps.rows;
  const AxisTaps& cols = taps.cols;
  const int src_w = cols.src;
  row_sum_.resize(src_w);
  row_mid_.resize(src_w);
  uint32_t* sum = row_sum_.data();
  uint16_t* mid = row_mid_.data();

  for (int y = 0; y < rows.dst; ++y) {
    const uint16_t* wy =
        rows.weights.data() + static_cast<size_t>(y) * rows.count;
    const uint8_t* line = src + static_cast<size_t>(rows.first[y]) * src_stride;
    const uint32_t w0 = wy[0];
    for (int x = 0; x < src_w; ++x)
      sum[x] = w0 * line[x];
    for (int i = 1; i < rows.count; ++i) {
      line += src_stride;
      const uint32_t w = wy[i];
      if (w == 0)
        continue;
      for (int x = 0; x < src_w; ++x)
        sum[x] += w * line[x];
    }
    for (int x = 0; x < src_w; ++x) {
      mid[x] = static_cast<uint16_t>((sum[x] + (1u << (kRowShift - 1))) >>
                                     kRowShift);
    }

    uint8_t* out = dst + static_cast<size_t>(y) * dst_stride;
    if (cols.src == 2 * cols.dst) {
      HalveLine(mid, cols.dst, out);
      continue;
    }
    switch (cols.count) {
      case 2:
        ScaleLine<2>(mid, cols, out);
        break;
      case 3:
        ScaleLine<3>(mid, cols, out);
        break;
      case 4:
        ScaleLine<4>(mid, cols, out);
        break;
      default:
        ScaleLine<0>(mid, cols, out);
        break;
    }
  }
}

const ScaledI420* FrameFitter::Fit(const I420View& in) {
  if (in.width <= 0 || in.height <= 0)
    return nullptr;
  const FrameSize target = FitWithin({in.width, in.height}, limit_);
  if (target.width >= in.width && target.height >= in.height)
    return nullptr;

  out_.width = target.width;
  out_.height = target.height;
  const int chroma_w = out_.stride_uv();
  const int chroma_h = (target.height + 1) / 2;
  out_.y.resize(static_cast<size_t>(target.width) * target.height);
  out_.u.resize(static_cast<size_t>(chroma_w) * chroma_h);
  out_.v.resize(out_.u.size());

  luma_.cols.Prepare(in.width, target.width);
  luma_.rows.Prepare(in.height, target.height);
  chroma_.cols.Prepare((in.width + 1) / 2, chroma_w);
  chroma_.rows.Prepare((in.height + 1) / 2, chroma_h);
  ScalePlane(in.y, in.stride_y, luma_, out_.y.data(), out_.stride_y());
  ScalePlane(in.u, in.stride_u, chroma_, out_.u.data(), chroma_w);
  ScalePlane(in.v, in.stride_v, chroma_, out_.v.data(), chroma_w);
  return &out_;
}

}  // namespace flutter_webrtc_plugin
