#include "flutter_onset_cover.h"

#include <algorithm>

namespace flutter_webrtc_plugin {

float OnsetCover::Step(float probability, const Tuning& tuning) {
  if (warmup_left_ > 0) {
    --warmup_left_;
    probability = 0.0f;
  }
  if (probability >= tuning.speech_probability) {
    ++run_;
    if (run_ == tuning.confirm_frames && quiet_ >= tuning.quiet_frames) {
      cover_left_ = tuning.cover_frames;
    }
  } else {
    // A run too short to be a word is part of the quiet around it.
    quiet_ = run_ >= tuning.confirm_frames ? 0 : std::min(quiet_ + run_, kQuietCap);
    quiet_ = std::min(quiet_ + 1, kQuietCap);
    run_ = 0;
  }
  const float release =
      1.0f / static_cast<float>(std::max(1, tuning.release_frames));
  if (cover_left_ > 0) {
    --cover_left_;
    weight_ = 1.0f;
  } else if (run_ >= tuning.confirm_frames) {
    weight_ = std::max(tuning.speaking_weight, weight_ - release);
  } else {
    weight_ = std::max(0.0f, weight_ - release);
  }
  return weight_;
}

}  // namespace flutter_webrtc_plugin
