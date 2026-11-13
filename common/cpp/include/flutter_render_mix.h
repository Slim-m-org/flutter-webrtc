#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_MIX_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_MIX_H_

#include <map>
#include <mutex>
#include <string>

#include "flutter_render_gains.h"
#include "rtc_audio_track.h"

namespace flutter_webrtc_plugin {

// Keeps libwebrtc's per-track volumes and the render limiter's boost in step
// as remote audio tracks come and go and as people move their sliders.
class RenderMix {
 public:
  // Never freed, for the reason InputGate::Instance gives.
  static RenderMix* Instance();

  // False for a track that is not remote audio, which libwebrtc scales itself.
  bool SetGain(const std::string& track_id, double gain);
  void TrackAdded(const std::string& track_id,
                  libwebrtc::scoped_refptr<libwebrtc::RTCAudioTrack> track);
  void TrackRemoved(const std::string& track_id);

 private:
  RenderMix() = default;
  void Apply();

  std::mutex mutex_;
  RenderGains gains_;
  std::map<std::string, libwebrtc::scoped_refptr<libwebrtc::RTCAudioTrack>>
      tracks_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_MIX_H_
