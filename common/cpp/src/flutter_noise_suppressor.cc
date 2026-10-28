#include "flutter_noise_suppressor.h"

#include <algorithm>
#include <cmath>
#include <mutex>

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

constexpr float kRampDb = 6.0f;

float FrameDb(const float* x) {
  double sum = 0.0;
  for (int i = 0; i < kFrameSize; ++i) sum += static_cast<double>(x[i]) * x[i];
  return 10.0f * std::log10(static_cast<float>(sum / kFrameSize) + 1e-3f);
}

// 0 to 1: how sure we are that DeepFilterNet turned down speech that rnnoise
// kept. A quiet frame is noise whoever is right, and a frame where the two
// agree has nothing to restore.
float ClippingEvidence(const float* rnnoise, const float* deep,
                       const NoiseSuppressor::StrongestTuning& tuning) {
  const float kept = FrameDb(rnnoise);
  const float lost = kept - FrameDb(deep);
  // The samples are on the int16 scale, so full scale is about 90 dB.
  const float loud = (kept - 90.31f - tuning.speech_floor_db) / kRampDb;
  const float clipped = (lost - tuning.clip_margin_db) / kRampDb;
  return std::clamp(std::min(loud, clipped), 0.0f, 1.0f);
}

}  // namespace

struct NoiseSuppressor::Channel {
  ~Channel() {
    if (state != nullptr) rnnoise_destroy(state);
  }

  void ResetStrongest() {
    cover.Reset();
    weight = 0.0f;
    probability = 0.0f;
    std::fill(rnnoise_late, rnnoise_late + kFrameSize, 0.0f);
  }

  DenoiseState* state = nullptr;
  RateConverter up;
  RateConverter down;
  float in48[kFrameSize];
  float out48[kFrameSize];
  // Strongest only: this frame's rnnoise output, and the last frame's, which
  // lines up with DeepFilterNet's one extra frame of delay.
  float rnnoise_now[kFrameSize];
  float rnnoise_late[kFrameSize] = {};
  float deep48[kFrameSize];
  OnsetCover cover;
  float weight = 0.0f;
  float probability = 0.0f;
  bool deep_failed = false;
};

NoiseSuppressor* NoiseSuppressor::Instance() {
  static NoiseSuppressor* suppressor = new NoiseSuppressor();
  return suppressor;
}

NoiseSuppressor::NoiseSuppressor() = default;

NoiseSuppressor::~NoiseSuppressor() { delete pending_.exchange(nullptr); }

void NoiseSuppressor::ConfigureStrongest(bool enabled) {
  std::lock_guard<std::mutex> lock(load_mutex_);
  strongest_.store(enabled);
  if (!enabled) {
    delete pending_.exchange(nullptr);
    model_failed_.store(false);
    return;
  }
  if (pending_.load() != nullptr || model_held_.load() || model_failed_.load()) {
    return;
  }
  std::unique_ptr<DeepFilter> loaded =
      tuning_.allow_model ? DeepFilter::Load(tuning_.model) : nullptr;
  if (loaded == nullptr) {
    model_failed_.store(true);
    return;
  }
  delete pending_.exchange(loaded.release());
}

void NoiseSuppressor::UpdateModel() {
  if (!strongest_.load()) {
    if (model_ != nullptr) DropModel(false);
    return;
  }
  if (model_ != nullptr || pending_.load() == nullptr) return;
  model_held_.store(true);
  model_.reset(pending_.exchange(nullptr));
  if (model_ == nullptr) model_held_.store(false);
}

void NoiseSuppressor::DropModel(bool failed) {
  model_.reset();
  model_held_.store(false);
  if (failed) model_failed_.store(true);
}

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
                                      int num_frames, DeepFilter* deep) {
  const bool native_rate = num_frames == kFrameSize;
  float* out = native_rate ? buffer : channel.out48;
  if (native_rate) {
    std::copy(buffer, buffer + kFrameSize, channel.in48);
  } else {
    channel.up.Convert(buffer, channel.in48);
  }
  float probability;
  if (deep == nullptr) {
    probability = rnnoise_process_frame(channel.state, out, channel.in48);
  } else {
    probability = ProcessStrongest(channel, *deep, out);
  }
  if (!native_rate) channel.down.Convert(channel.out48, buffer);
  if (!std::isfinite(probability) || !AllFinite(buffer, num_frames)) {
    rnnoise_init(channel.state, nullptr);
    channel.up.Reset();
    channel.down.Reset();
    channel.ResetStrongest();
    std::fill(buffer, buffer + num_frames, 0.0f);
    return 0.0f;
  }
  return probability;
}

// Writes the blend of DeepFilterNet's output and rnnoise's to `out` and
// returns the speech probability that goes with it.
float NoiseSuppressor::ProcessStrongest(Channel& channel, DeepFilter& deep,
                                        float* out) {
  const float probability = rnnoise_process_frame(
      channel.state, channel.rnnoise_now, channel.in48);
  if (!deep.Process(channel.in48, channel.deep48)) {
    channel.deep_failed = true;
    std::copy(channel.rnnoise_now, channel.rnnoise_now + kFrameSize, out);
    return probability;
  }
  const float from = channel.weight;
  const float to = channel.cover.Step(probability, tuning_.onset) *
                   ClippingEvidence(channel.rnnoise_late, channel.deep48,
                                    tuning_);
  for (int i = 0; i < kFrameSize; ++i) {
    const float w = from + (to - from) * static_cast<float>(i + 1) /
                               static_cast<float>(kFrameSize);
    out[i] = w * channel.rnnoise_late[i] + (1.0f - w) * channel.deep48[i];
  }
  std::copy(channel.rnnoise_now, channel.rnnoise_now + kFrameSize,
            channel.rnnoise_late);
  channel.weight = to;
  // The audio is a frame later than rnnoise's, and so is the probability
  // the gate pairs it with.
  const float late = channel.probability;
  channel.probability = probability;
  return late;
}

float NoiseSuppressor::ProcessPlanar(float* const* channels, int num_channels,
                                     int num_frames) {
  UpdateModel();
  const bool strongest = strongest_.load();
  const bool run = (enabled_.load() || strongest) && channels != nullptr &&
                   num_channels >= 1 && num_channels <= kMaxChannels &&
                   num_frames >= 1 && num_frames <= kMaxFrames;
  bool ready = run && Prepare(num_channels, num_frames);
  for (int c = 0; ready && c < num_channels; ++c) {
    ready = channels[c] != nullptr;
  }
  active_.store(ready);
  if (!ready) {
    was_active_ = false;
    strongest_active_.store(false);
    return kNoSpeechProbability;
  }
  if (!was_active_) {
    for (int c = 0; c < num_channels; ++c) {
      rnnoise_init(channels_[c]->state, nullptr);
      channels_[c]->up.Reset();
      channels_[c]->down.Reset();
      channels_[c]->ResetStrongest();
    }
  }
  was_active_ = true;

  float best = 0.0f;
  bool deep_ran = false;
  for (int c = 0; c < num_channels; ++c) {
    DeepFilter* deep = c == 0 ? model_.get() : nullptr;
    Channel& channel = *channels_[c];
    channel.deep_failed = false;
    best = std::max(best,
                    ProcessChannel(channel, channels[c], num_frames, deep));
    deep_ran = deep_ran || (deep != nullptr && !channel.deep_failed);
    if (channel.deep_failed) DropModel(true);
  }
  strongest_active_.store(deep_ran);
  return best;
}

float NoiseSuppressor::Process(float* buffer, int num_frames) {
  return ProcessPlanar(&buffer, 1, num_frames);
}

}  // namespace flutter_webrtc_plugin
