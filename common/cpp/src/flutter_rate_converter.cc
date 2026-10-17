#include "flutter_rate_converter.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace flutter_webrtc_plugin {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Zero crossings of the low-pass kernel on each side, and how much of the
// narrower Nyquist band it keeps; the rest is the transition band.
constexpr double kLobes = 8.0;
constexpr double kPassband = 0.95;

double Sinc(double x) {
  if (std::fabs(x) < 1e-9) return 1.0;
  return std::sin(kPi * x) / (kPi * x);
}

double Blackman(double x) {
  if (std::fabs(x) >= 1.0) return 0.0;
  return 0.42 + 0.5 * std::cos(kPi * x) + 0.08 * std::cos(2.0 * kPi * x);
}

}  // namespace

bool RateConverter::Configure(int in_rate_hz, int out_rate_hz) {
  kernels_.clear();
  if (in_rate_hz <= 0 || out_rate_hz <= 0 || in_rate_hz % 100 != 0 ||
      out_rate_hz % 100 != 0) {
    return false;
  }
  in_frames_ = in_rate_hz / 100;
  out_frames_ = out_rate_hz / 100;
  const int divisor = std::gcd(in_rate_hz, out_rate_hz);
  const int step = in_rate_hz / divisor;
  const int phases = out_rate_hz / divisor;

  // Cutoff relative to the input Nyquist; below 1 only when downsampling.
  const double cutoff =
      kPassband * std::min(1.0, static_cast<double>(out_rate_hz) / in_rate_hz);
  half_taps_ = static_cast<int>(std::ceil(kLobes / cutoff));
  taps_ = 2 * half_taps_;

  base_.assign(out_frames_, 0);
  phase_.assign(out_frames_, 0);
  for (int j = 0; j < out_frames_; ++j) {
    base_[j] = (j * step) / phases;
    phase_[j] = (j * step) % phases;
  }

  kernels_.assign(static_cast<size_t>(phases) * taps_, 0.0f);
  for (int p = 0; p < phases; ++p) {
    const double frac = static_cast<double>(p) / phases;
    double sum = 0.0;
    std::vector<double> row(taps_);
    for (int m = 0; m < taps_; ++m) {
      const double u = frac - half_taps_ + m;
      row[m] = cutoff * Sinc(cutoff * u) * Blackman(u / half_taps_);
      sum += row[m];
    }
    for (int m = 0; m < taps_; ++m) {
      kernels_[static_cast<size_t>(p) * taps_ + m] =
          static_cast<float>(row[m] / sum);
    }
  }
  scratch_.assign(static_cast<size_t>(taps_ - 1 + in_frames_), 0.0f);
  return true;
}

void RateConverter::Reset() {
  std::fill(scratch_.begin(), scratch_.end(), 0.0f);
}

void RateConverter::Convert(const float* in, float* out) {
  float* x = scratch_.data();
  const int history = taps_ - 1;
  std::copy(in, in + in_frames_, x + history);
  for (int j = 0; j < out_frames_; ++j) {
    const float* kernel = &kernels_[static_cast<size_t>(phase_[j]) * taps_];
    const float* newest = x + history + base_[j];
    float acc = 0.0f;
    for (int m = 0; m < taps_; ++m) acc += kernel[m] * newest[-m];
    out[j] = acc;
  }
  std::copy(x + in_frames_, x + in_frames_ + history, x);
}

}  // namespace flutter_webrtc_plugin
