#include "flutter_render_mix.h"

#include <vector>

#include "flutter_render_limiter.h"

namespace flutter_webrtc_plugin {

RenderMix* RenderMix::Instance() {
  static RenderMix* mix = new RenderMix();
  return mix;
}

bool RenderMix::SetGain(const std::string& track_id, double gain) {
  bool lead = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tracks_.find(track_id) == tracks_.end()) return false;
    gains_.Set(track_id, gain);
    lead = Apply();
  }
  if (lead) Push();
  return true;
}

void RenderMix::TrackAdded(
    const std::string& track_id,
    libwebrtc::scoped_refptr<libwebrtc::RTCAudioTrack> track) {
  bool lead = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tracks_[track_id] = track;
    lead = Apply();
  }
  if (lead) Push();
}

void RenderMix::TrackRemoved(const std::string& track_id) {
  bool lead = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tracks_.erase(track_id);
    gains_.Forget(track_id);
    lead = Apply();
  }
  if (lead) Push();
}

bool RenderMix::Apply() {
  std::vector<std::string> live;
  live.reserve(tracks_.size());
  for (const auto& entry : tracks_) live.push_back(entry.first);
  RenderLimiter::Instance()->SetBoost(static_cast<float>(gains_.Boost(live)));
  Pending pending;
  for (const auto& v : gains_.Volumes(live)) {
    pending.volumes.emplace_back(tracks_[v.track_id], v.volume);
  }
  latest_ = std::move(pending);
  has_latest_ = true;
  if (pushing_) return false;
  pushing_ = true;
  return true;
}

void RenderMix::Push() {
  for (;;) {
    Pending pending;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!has_latest_) {
        pushing_ = false;
        return;
      }
      pending = std::move(latest_);
      has_latest_ = false;
    }
    for (const auto& entry : pending.volumes) {
      entry.first->SetVolume(entry.second);
    }
  }
}

}  // namespace flutter_webrtc_plugin
