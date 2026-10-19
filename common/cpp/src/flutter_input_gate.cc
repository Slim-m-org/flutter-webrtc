#include "flutter_input_gate.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace flutter_webrtc_plugin {

namespace {

// Each Process call is one 10ms frame.
constexpr int kFrameMs = 10;
// How long the gate stays open after the last loud frame, so the ends of
// words and the gaps between them are not cut.
constexpr int kHoldFrames = 300 / kFrameMs;
// With a speech probability: above this a frame is speech; below the low
// mark it is not, and the tail after it is cut short. Without speech a
// frame must also be this far over the threshold to open the gate, and last
// two frames, so a keyboard click does not.
constexpr float kSpeechOpen = 0.6f;
constexpr float kSpeechLow = 0.3f;
constexpr float kLoudMarginDb = 15.0f;
// Once open, the gate keeps counting frames this far under the threshold as
// voice, so a word fading out does not flutter the gate at the threshold.
constexpr float kCloseHysteresisDb = 5.0f;
// The audio is delayed by this many frames, so the gate is already open when
// the frame that opened it arrives and a soft word start is not clipped.
constexpr int kLookaheadFrames = 2;
constexpr int kLoudConfirmFrames = 2;
constexpr int kLowSpeechHoldFrames = 100 / kFrameMs;
// Gain ramps, so opening and closing never click.
constexpr float kOpenRampMs = 5.0f;
constexpr float kCloseRampMs = 60.0f;
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
  ResetState();
}

void InputGate::Reset(int /*new_rate*/) { ResetState(); }

void InputGate::ResetState() {
  hold_frames_left_ = 0;
  loud_run_frames_ = 0;
  recent_voice_ = 0;
  gain_ = enabled_.load() ? 0.0f : 1.0f;
  delay_.clear();
  delay_pos_ = 0;
  peak_db_.store(kFloorDb);
}

void InputGate::Delay(int num_frames, float* buffer) {
  const size_t size = static_cast<size_t>(num_frames) * kLookaheadFrames;
  if (delay_.size() != size) {
    delay_.assign(size, 0.0f);
    delay_pos_ = 0;
  }
  for (int i = 0; i < num_frames; ++i) {
    std::swap(buffer[i], delay_[delay_pos_]);
    if (++delay_pos_ == size) delay_pos_ = 0;
  }
}

void InputGate::Process(int /*num_bands*/, int num_frames, int /*buffer_size*/,
                        float* buffer) {
  ProcessWithSpeech(num_frames, buffer, -1.0f);
}

void InputGate::ProcessWithSpeech(int num_frames, float* buffer,
                                  float speech_probability) {
  if (buffer == nullptr || num_frames <= 0) return;

  double sum = 0.0;
  for (int i = 0; i < num_frames; ++i) {
    sum += static_cast<double>(buffer[i]) * buffer[i];
  }
  const float rms =
      static_cast<float>(std::sqrt(sum / static_cast<double>(num_frames)));
  const float frame_db =
      (std::max)(kFloorDb, 20.0f * std::log10(rms / kFullScale + 1e-9f));
  float peak = peak_db_.load();
  while (frame_db > peak && !peak_db_.compare_exchange_weak(peak, frame_db)) {
  }

  const float threshold_db = threshold_db_.load();
  const bool was_open = hold_frames_left_ > 0;
  const bool over_open = frame_db >= threshold_db;
  const bool over =
      was_open ? frame_db >= threshold_db - kCloseHysteresisDb : over_open;
  bool voice = true;
  if (speech_probability < 0.0f) {
    if (over) {
      hold_frames_left_ = kHoldFrames;
    } else if (hold_frames_left_ > 0) {
      --hold_frames_left_;
    } else {
      voice = false;
    }
  } else {
    const bool speech = over && speech_probability > kSpeechOpen;
    const bool loud = over_open && frame_db >= threshold_db + kLoudMarginDb;
    loud_run_frames_ = loud && !speech ? loud_run_frames_ + 1 : 0;
    if (speech || loud_run_frames_ >= kLoudConfirmFrames) {
      hold_frames_left_ = kHoldFrames;
    } else if (hold_frames_left_ > 0) {
      if (over) {
        hold_frames_left_ =
            (std::max)(hold_frames_left_, kLowSpeechHoldFrames);
      }
      if (speech_probability < kSpeechLow) {
        hold_frames_left_ = (std::min)(hold_frames_left_, kLowSpeechHoldFrames);
      }
      --hold_frames_left_;
    } else {
      voice = false;
    }
  }
  voice_.store(voice);
  Delay(num_frames, buffer);
  // The delayed frame is two frames older than the decision, so it stays
  // open while any of the frames it overlaps counted as voice.
  recent_voice_ = ((recent_voice_ << 1) | (voice ? 1u : 0u)) &
                  ((1u << (kLookaheadFrames + 1)) - 1);
  const bool want_open = recent_voice_ != 0 || !enabled_.load();

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
    gain_ = want_open ? (std::min)(1.0f, gain_ + step)
                      : (std::max)(0.0f, gain_ - step);
    buffer[i] *= gain_;
  }
}

}  // namespace flutter_webrtc_plugin
