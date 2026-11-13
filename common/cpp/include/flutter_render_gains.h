#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_GAINS_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_GAINS_H_

#include <map>
#include <string>
#include <vector>

namespace flutter_webrtc_plugin {

// What libwebrtc's own per-track volume must be set to so a gain above 100% never
// runs inside it, where it clips: every live track is scaled down by the
// loudest request, and the mix is scaled back up in float afterwards.
class RenderGains {
 public:
  struct Volume {
    std::string track_id;
    double volume;
  };

  void Set(const std::string& track_id, double gain);
  void Forget(const std::string& track_id);

  // The multiplier for the mix, 1 or more, given the tracks that exist now.
  double Boost(const std::vector<std::string>& live) const;

  // The volume libwebrtc is given for each live track under that boost.
  std::vector<Volume> Volumes(const std::vector<std::string>& live) const;

 private:
  double GainOf(const std::string& track_id) const;

  std::map<std::string, double> gains_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_RENDER_GAINS_H_
