#include "flutter_render_mix.h"

#include <vector>

#include "flutter_render_limiter.h"

namespace flutter_webrtc_plugin {

RenderMix* RenderMix::Instance() {
  static RenderMix* mix = new RenderMix();
  return mix;
}

bool RenderMix::SetGain(const std::string& track_id, double gain) {
  Pending pending;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tracks_.find(track_id) == tracks_.end()) return false;
    gains_.Set(track_id, gain);
    pending = Apply();
  }
  Push(pending);
  return true;
}

void RenderMix::TrackAdded(
    const std::string& track_id,
    libwebrtc::scoped_refptr<libwebrtc::RTCAudioTrack> track) {
  Pending pending;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tracks_[track_id] = track;
    pending = Apply();
  }
  Push(pending);
}

void RenderMix::TrackRemoved(const std::string& track_id) {
  Pending pending;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tracks_.erase(track_id);
    gains_.Forget(track_id);
    pending = Apply();
  }
  Push(pending);
}

RenderMix::Pending RenderMix::Apply() {
  std::vector<std::string> live;
  live.reserve(tracks_.size());
  for (const auto& entry : tracks_) live.push_back(entry.first);
  RenderLimiter::Instance()->SetBoost(static_cast<float>(gains_.Boost(live)));
  Pending pending;
  pending.generation = ++generation_;
  for (const auto& v : gains_.Volumes(live)) {
    pending.volumes.emplace_back(tracks_[v.track_id], v.volume);
  }
  return pending;
}

void RenderMix::Push(const Pending& pending) {
  for (const auto& entry : pending.volumes) {
    if (generation_.load() != pending.generation) return;
    entry.first->SetVolume(entry.second);
  }
}

}  // namespace flutter_webrtc_plugin
