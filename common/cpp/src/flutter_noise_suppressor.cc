#include "flutter_noise_suppressor.h"

#include <algorithm>
#include <cmath>

#include "rnnoise.h"

namespace flutter_webrtc_plugin {

namespace {

// rnnoise's fixed frame: 10ms at 48 kHz.
constexpr int kRnnoiseRate = 48000;
constexpr int kFrameSize = 480;
// 480 kHz worth of samples is already far past any real capture device.
constexpr int kMaxFrames = 4800;

bool AllFinite(const float* x, int n) {
  for (int i = 0; i < n; ++i) {
    if (!std::isfinite(x[i])) return false;
  }
  return true;
}

}  // namespace

struct NoiseSuppressor::Channel {
  ~Channel() {
    if (state != nullptr) rnnoise_destroy(state);
  }

  DenoiseState* state = nullptr;
  RateConverter up;
  RateConverter down;
  float in48[kFrameSize];
  float out48[kFrameSize];
};

NoiseSuppressor* NoiseSuppressor::Instance() {
  static NoiseSuppressor* suppressor = new NoiseSuppressor();
  return suppressor;
}

NoiseSuppressor::NoiseSuppressor() = default;
NoiseSuppressor::~NoiseSuppressor() = default;

// Allocates only when the capture rate or channel count changes.
bool NoiseSuppressor::Prepare(int num_channels, int num_frames) {
  const int rate_hz = num_frames * 100;
  for (int c = 0; c < num_channels; ++c) {
    Channel* channel = channels_[c].get();
    if (channel != nullptr && rate_hz == rate_hz_ && channel->state != nullptr) {
      continue;
    }
    if (channel == nullptr) {
      channels_[c] = std::make_unique<Channel>();
      channel = channels_[c].get();
    }
    if (channel->state == nullptr) channel->state = rnnoise_create(nullptr);
    if (channel->state == nullptr) return false;
    if (rate_hz != kRnnoiseRate &&
        (!channel->up.Configure(rate_hz, kRnnoiseRate) ||
         !channel->down.Configure(kRnnoiseRate, rate_hz))) {
      return false;
    }
  }
  rate_hz_ = rate_hz;
  return true;
}

float NoiseSuppressor::ProcessChannel(Channel& channel, float* buffer,
                                      int num_frames) {
  float probability;
  if (num_frames == kFrameSize) {
    std::copy(buffer, buffer + kFrameSize, channel.in48);
    probability = rnnoise_process_frame(channel.state, buffer, channel.in48);
  } else {
    channel.up.Convert(buffer, channel.in48);
    probability =
        rnnoise_process_frame(channel.state, channel.out48, channel.in48);
    channel.down.Convert(channel.out48, buffer);
  }
  if (!std::isfinite(probability) || !AllFinite(buffer, num_frames)) {
    rnnoise_init(channel.state, nullptr);
    channel.up.Reset();
    channel.down.Reset();
    std::fill(buffer, buffer + num_frames, 0.0f);
    return 0.0f;
  }
  return probability;
}

float NoiseSuppressor::ProcessPlanar(float* const* channels, int num_channels,
                                     int num_frames) {
  const bool run = enabled_.load() && channels != nullptr &&
                   num_channels >= 1 && num_channels <= kMaxChannels &&
                   num_frames >= 1 && num_frames <= kMaxFrames;
  bool ready = run && Prepare(num_channels, num_frames);
  for (int c = 0; ready && c < num_channels; ++c) {
    ready = channels[c] != nullptr;
  }
  active_.store(ready);
  if (!ready) {
    was_active_ = false;
    return kNoSpeechProbability;
  }
  if (!was_active_) {
    for (int c = 0; c < num_channels; ++c) {
      rnnoise_init(channels_[c]->state, nullptr);
      channels_[c]->up.Reset();
      channels_[c]->down.Reset();
    }
  }
  was_active_ = true;

  float best = 0.0f;
  for (int c = 0; c < num_channels; ++c) {
    best = std::max(best,
                    ProcessChannel(*channels_[c], channels[c], num_frames));
  }
  return best;
}

float NoiseSuppressor::Process(float* buffer, int num_frames) {
  return ProcessPlanar(&buffer, 1, num_frames);
}

}  // namespace flutter_webrtc_plugin
