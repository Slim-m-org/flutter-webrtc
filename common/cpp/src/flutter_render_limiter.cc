#include "flutter_render_limiter.h"

#include <algorithm>
#include <cmath>

namespace flutter_webrtc_plugin {

namespace {

constexpr float kFullScale = 32767.0f;
// Below this the curve is the identity; above it tanh bends towards full scale.
constexpr float kKnee = 0.6f * kFullScale;
constexpr float kMaxBoost = 8.0f;

float Shape(float x) {
  const float magnitude = std::fabs(x);
  if (magnitude <= kKnee) return x;
  const float room = kFullScale - kKnee;
  const float bent = kKnee + room * std::tanh((magnitude - kKnee) / room);
  return std::copysign(bent, x);
}

}  // namespace

RenderLimiter* RenderLimiter::Instance() {
  static RenderLimiter* limiter = new RenderLimiter();
  return limiter;
}

void RenderLimiter::SetBoost(float boost) {
  boost_.store(std::clamp(boost, 1.0f, kMaxBoost));
}

void RenderLimiter::Apply(float* buffer, int count, float boost) {
  if (buffer == nullptr || count <= 0 || boost <= 1.0f) return;
  for (int i = 0; i < count; ++i) {
    buffer[i] = Shape(buffer[i] * boost);
  }
}

void RenderLimiter::Initialize(int /*sample_rate_hz*/, int num_channels) {
  channels_.store((std::max)(1, num_channels));
}

void RenderLimiter::Process(int num_bands, int num_frames,
                            int /*buffer_size*/, float* buffer) {
  if (buffer == nullptr || num_frames <= 0) return;
  const float boost = boost_.load();
  // libwebrtc hands over channel 0 only; the channels sit one after another in its buffer.
  const int stride = num_frames * (std::max)(1, num_bands);
  for (int c = 0; c < channels_.load(); ++c) {
    Apply(buffer + c * stride, stride, boost);
  }
}

}  // namespace flutter_webrtc_plugin
