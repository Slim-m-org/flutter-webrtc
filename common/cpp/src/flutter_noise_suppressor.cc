#include "flutter_noise_suppressor.h"

#include <algorithm>
#include <cmath>
#include <future>
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

NoiseSuppressor::~NoiseSuppressor() {
  worker_.Stop();
  delete pending_.exchange(nullptr);
  delete retired_.exchange(nullptr);
}

void NoiseSuppressor::ConfigureStrongest(bool enabled) {
  std::lock_guard<std::mutex> lock(load_mutex_);
  strongest_.store(enabled);
  if (!enabled) {
    delete pending_.exchange(nullptr);
    model_failed_.store(false);
    return;
  }
  StartLoad();
}

void NoiseSuppressor::WaitForStrongestModel() {
  for (;;) {
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    if (!worker_.Post([done = std::move(done)] { done->set_value(); })) return;
    finished.wait();
    // A task that ran before the marker may have queued the load behind it.
    std::lock_guard<std::mutex> lock(load_mutex_);
    if (!loading_) return;
  }
}

void NoiseSuppressor::StartLoad() {
  if (!strongest_.load() || loading_ || model_held_.load() ||
      model_failed_.load() || pending_.load() != nullptr) {
    return;
  }
  loading_ = true;
  worker_.Post([this, params = tuning_.model, allow = tuning_.allow_model] {
    LoadModel(params, allow);
  });
}

void NoiseSuppressor::LoadModel(const DeepFilter::Params& params,
                                bool allow_model) {
  std::unique_ptr<DeepFilter> loaded =
      allow_model ? DeepFilter::Load(params) : nullptr;
  std::lock_guard<std::mutex> lock(load_mutex_);
  loading_ = false;
  if (!strongest_.load()) return;
  if (loaded == nullptr) {
    model_failed_.store(true);
    return;
  }
  pending_.store(loaded.release());
}

// Frees what the audio thread handed back, then loads again if Strongest was
// asked for while that was going on.
void NoiseSuppressor::Settle() {
  delete retired_.exchange(nullptr);
  std::lock_guard<std::mutex> lock(load_mutex_);
  model_held_.store(false);
  StartLoad();
}

void NoiseSuppressor::UpdateModel() {
  if (!strongest_.load()) {
    if (model_ != nullptr) RetireModel(false);
    return;
  }
  if (model_ != nullptr || pending_.load() == nullptr) return;
  model_held_.store(true);
  model_.reset(pending_.exchange(nullptr));
  if (model_ == nullptr) worker_.Post([this] { Settle(); });
}

void NoiseSuppressor::RetireModel(bool failed) {
  retired_.store(model_.release());
  if (failed) model_failed_.store(true);
  worker_.Post([this] { Settle(); });
}

// Allocates only when the capture rate changes.
bool NoiseSuppressor::Prepare(int num_frames) {
  const int rate_hz = num_frames * 100;
  if (channel_ != nullptr && rate_hz == rate_hz_) return true;
  // Stays zero if anything below fails, so the next frame configures again.
  rate_hz_ = 0;
  if (channel_ == nullptr) channel_ = std::make_unique<Channel>();
  if (channel_->state == nullptr) channel_->state = rnnoise_create(nullptr);
  if (channel_->state == nullptr) return false;
  if (rate_hz != kRnnoiseRate &&
      (!channel_->up.Configure(rate_hz, kRnnoiseRate) ||
       !channel_->down.Configure(kRnnoiseRate, rate_hz))) {
    return false;
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

float NoiseSuppressor::Process(float* buffer, int num_frames) {
  UpdateModel();
  const bool run = (enabled_.load() || strongest_.load()) &&
                   buffer != nullptr && num_frames >= 1 &&
                   num_frames <= kMaxFrames;
  const bool ready = run && Prepare(num_frames);
  active_.store(ready);
  if (!ready) {
    was_active_ = false;
    strongest_active_.store(false);
    return kNoSpeechProbability;
  }
  if (!was_active_) {
    rnnoise_init(channel_->state, nullptr);
    channel_->up.Reset();
    channel_->down.Reset();
    channel_->ResetStrongest();
  }
  was_active_ = true;

  DeepFilter* deep = model_.get();
  channel_->deep_failed = false;
  const float probability = ProcessChannel(*channel_, buffer, num_frames, deep);
  if (channel_->deep_failed) RetireModel(true);
  strongest_active_.store(deep != nullptr && !channel_->deep_failed);
  return probability;
}

}  // namespace flutter_webrtc_plugin
