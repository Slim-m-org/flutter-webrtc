#ifndef FLUTTER_WEBRTC_PLUGIN_DEFAULT_AUDIO_ENDPOINT_H_
#define FLUTTER_WEBRTC_PLUGIN_DEFAULT_AUDIO_ENDPOINT_H_

#include <string>

namespace flutter_webrtc_plugin {

// Windows only: the endpoint id the call should use when the user picked no
// device, or empty when it cannot be read. Playback takes the console default
// so the call plays where every other app does. Capture takes the
// communications default, which is the one a headset claims; the console
// default capture device is often a webcam, line in or virtual device that
// reads silence. Ducking is opted out separately (audio_ducking_opt_out.h).
std::string DefaultCallEndpointId(bool capture);

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_DEFAULT_AUDIO_ENDPOINT_H_
