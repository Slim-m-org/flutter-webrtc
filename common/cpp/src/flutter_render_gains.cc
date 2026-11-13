#include "flutter_render_gains.h"

#include <algorithm>

namespace flutter_webrtc_plugin {

void RenderGains::Set(const std::string& track_id, double gain) {
  if (gain == 1.0) {
    gains_.erase(track_id);
  } else {
    gains_[track_id] = gain;
  }
}

void RenderGains::Forget(const std::string& track_id) {
  gains_.erase(track_id);
}

double RenderGains::GainOf(const std::string& track_id) const {
  auto found = gains_.find(track_id);
  return found == gains_.end() ? 1.0 : found->second;
}

double RenderGains::Boost(const std::vector<std::string>& live) const {
  double boost = 1.0;
  for (const auto& id : live) boost = std::max(boost, GainOf(id));
  return boost;
}

std::vector<RenderGains::Volume> RenderGains::Volumes(
    const std::vector<std::string>& live) const {
  const double boost = Boost(live);
  std::vector<Volume> volumes;
  volumes.reserve(live.size());
  for (const auto& id : live) volumes.push_back({id, GainOf(id) / boost});
  return volumes;
}

}  // namespace flutter_webrtc_plugin
