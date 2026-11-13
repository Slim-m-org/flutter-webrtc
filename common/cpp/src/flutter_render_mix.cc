#include "flutter_render_mix.h"

#include <vector>

#include "flutter_render_limiter.h"

namespace flutter_webrtc_plugin {

RenderMix* RenderMix::Instance() {
  static RenderMix* mix = new RenderMix();
  return mix;
}

bool RenderMix::SetGain(const std::string& track_id, double gain) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (tracks_.find(track_id) == tracks_.end()) return false;
  gains_.Set(track_id, gain);
  Apply();
  return true;
}

void RenderMix::TrackAdded(
    const std::string& track_id,
    libwebrtc::scoped_refptr<libwebrtc::RTCAudioTrack> track) {
  std::lock_guard<std::mutex> lock(mutex_);
  tracks_[track_id] = track;
  Apply();
}

void RenderMix::TrackRemoved(const std::string& track_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  tracks_.erase(track_id);
  gains_.Forget(track_id);
  Apply();
}

void RenderMix::Apply() {
  std::vector<std::string> live;
  live.reserve(tracks_.size());
  for (const auto& entry : tracks_) live.push_back(entry.first);
  RenderLimiter::Instance()->SetBoost(static_cast<float>(gains_.Boost(live)));
  for (const auto& v : gains_.Volumes(live)) {
    tracks_[v.track_id]->SetVolume(v.volume);
  }
}

}  // namespace flutter_webrtc_plugin
