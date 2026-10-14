#include "flutter_noise_suppressor.h"

#include <algorithm>

#include "rnnoise.h"

namespace flutter_webrtc_plugin {

namespace {

// rnnoise's fixed frame: 10ms at 48 kHz.
constexpr int kFrameSize = 480;

}  // namespace

NoiseSuppressor* NoiseSuppressor::Instance() {
  static NoiseSuppressor* suppressor = new NoiseSuppressor();
  return suppressor;
}

void NoiseSuppressor::Process(float* buffer, int num_frames) {
  const bool run =
      enabled_.load() && buffer != nullptr && num_frames == kFrameSize;
  active_.store(run);
  if (!run) {
    was_active_ = false;
    return;
  }
  if (state_ == nullptr) {
    state_ = rnnoise_create(nullptr);
    if (state_ == nullptr) {
      active_.store(false);
      return;
    }
  } else if (!was_active_) {
    rnnoise_init(state_, nullptr);
  }
  was_active_ = true;

  float input[kFrameSize];
  std::copy(buffer, buffer + kFrameSize, input);
  rnnoise_process_frame(state_, buffer, input);
}

}  // namespace flutter_webrtc_plugin
