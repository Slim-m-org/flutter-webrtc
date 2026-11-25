#include "flutter_mic_sample_tap.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace flutter_webrtc_plugin {

namespace {

constexpr int kMaxSeconds = 120;

int16_t ToInt16(float v) {
  const float clamped = (std::min)(32767.0f, (std::max)(-32768.0f, v));
  return static_cast<int16_t>(std::lrintf(clamped));
}

}  // namespace

MicSampleTap* MicSampleTap::Instance() {
  static MicSampleTap* tap = new MicSampleTap();
  return tap;
}

void MicSampleTap::Start(int max_seconds) {
  const int seconds = (std::min)(kMaxSeconds, (std::max)(1, max_seconds));
  const size_t limit = static_cast<size_t>(seconds) * kRateHz;
  std::vector<int16_t> samples;
  samples.reserve(limit);
  RateConverter converter;
  std::vector<float> converted;
  int converter_rate_hz = 0;
  const int rate_hz = capture_rate_hz_.load();
  if (rate_hz != kRateHz && converter.Configure(rate_hz, kRateHz)) {
    converted.assign(converter.OutFrames(), 0.0f);
    converter_rate_hz = rate_hz;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  samples_.swap(samples);
  limit_ = limit;
  kept_ = 0;
  converter_ = std::move(converter);
  converted_ = std::move(converted);
  converter_rate_hz_ = converter_rate_hz;
  skipped_.store(0);
  dropped_.store(0);
  full_.store(false);
  armed_.store(true);
}

void MicSampleTap::Stop() { armed_.store(false); }

void MicSampleTap::Feed(const float* frame, int num_frames) {
  if (!armed_.load() || frame == nullptr || num_frames <= 0) return;
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    dropped_.fetch_add(1);
    return;
  }
  if (!armed_.load()) return;
  const int rate_hz = num_frames * 100;
  const float* source = frame;
  int count = num_frames;
  if (rate_hz != kRateHz) {
    if (rate_hz != converter_rate_hz_) {
      if (!converter_.Configure(rate_hz, kRateHz)) {
        skipped_.fetch_add(1);
        return;
      }
      converted_.assign(converter_.OutFrames(), 0.0f);
      converter_rate_hz_ = rate_hz;
    }
    converter_.Convert(frame, converted_.data());
    source = converted_.data();
    count = converter_.OutFrames();
  }
  const size_t room = limit_ - kept_;
  const size_t take = (std::min)(room, static_cast<size_t>(count));
  for (size_t i = 0; i < take; ++i) samples_.push_back(ToInt16(source[i]));
  kept_ += take;
  if (kept_ >= limit_) {
    full_.store(true);
    armed_.store(false);
  }
}

std::vector<int16_t> MicSampleTap::Take(const std::function<void()>& between) {
  size_t room = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    room = limit_ - kept_;
  }
  // Reserved off the lock: what is kept after the swap never exceeds `room`.
  std::vector<int16_t> out;
  if (armed_.load()) out.reserve(room);
  if (between) between();
  std::lock_guard<std::mutex> lock(mutex_);
  // A Start in between raised the limit, and Feed must not grow the buffer.
  if (out.capacity() < limit_ - kept_) out.reserve(limit_ - kept_);
  out.swap(samples_);
  return out;
}

}  // namespace flutter_webrtc_plugin
