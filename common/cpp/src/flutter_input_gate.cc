#include "flutter_input_gate.h"

#include <algorithm>
#include <cmath>

namespace flutter_webrtc_plugin {

namespace {

// Each Process call is one 10ms frame.
constexpr int kFrameMs = 10;
// How long the gate stays open after the last loud frame, so the ends of
// words and the gaps between them are not cut.
constexpr int kHoldFrames = 300 / kFrameMs;
// Gain ramps, so opening and closing never click.
constexpr float kOpenRampMs = 5.0f;
constexpr float kCloseRampMs = 60.0f;
// How fast the meter falls back, in dB per frame; it rises instantly.
constexpr float kMeterDecayDb = 1.5f;
constexpr float kFloorDb = -100.0f;
// The float samples are on the int16 scale.
constexpr float kFullScale = 32768.0f;

}  // namespace

InputGate* InputGate::Instance() {
  static InputGate* gate = new InputGate();
  return gate;
}

void InputGate::Configure(bool enabled, float threshold_db) {
  threshold_db_.store(std::clamp(threshold_db, kFloorDb, 0.0f));
  enabled_.store(enabled);
}

void InputGate::Initialize(int /*sample_rate_hz*/, int /*num_channels*/) {
  hold_frames_left_ = 0;
}

void InputGate::Reset(int /*new_rate*/) { hold_frames_left_ = 0; }

void InputGate::Process(int /*num_bands*/, int num_frames, int /*buffer_size*/,
                        float* buffer) {
  if (buffer == nullptr || num_frames <= 0) return;

  double sum = 0.0;
  for (int i = 0; i < num_frames; ++i) {
    sum += static_cast<double>(buffer[i]) * buffer[i];
  }
  const float rms =
      static_cast<float>(std::sqrt(sum / static_cast<double>(num_frames)));
  const float frame_db =
      std::max(kFloorDb, 20.0f * std::log10(rms / kFullScale + 1e-9f));
  const float metered = std::max(frame_db, level_db_.load() - kMeterDecayDb);
  level_db_.store(std::max(kFloorDb, metered));

  bool voice = true;
  if (frame_db >= threshold_db_.load()) {
    hold_frames_left_ = kHoldFrames;
  } else if (hold_frames_left_ > 0) {
    --hold_frames_left_;
  } else {
    voice = false;
  }
  voice_.store(voice);
  const bool want_open = voice || !enabled_.load();

  const float target = want_open ? 1.0f : 0.0f;
  if (gain_ == target) {
    if (target == 0.0f) std::fill(buffer, buffer + num_frames, 0.0f);
    return;
  }
  const float ramp_ms = want_open ? kOpenRampMs : kCloseRampMs;
  const float samples_per_ms =
      static_cast<float>(num_frames) / static_cast<float>(kFrameMs);
  const float step = 1.0f / (ramp_ms * samples_per_ms);
  for (int i = 0; i < num_frames; ++i) {
    gain_ = want_open ? std::min(1.0f, gain_ + step)
                      : std::max(0.0f, gain_ - step);
    buffer[i] *= gain_;
  }
}

}  // namespace flutter_webrtc_plugin
