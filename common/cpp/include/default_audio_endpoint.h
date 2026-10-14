#ifndef FLUTTER_WEBRTC_PLUGIN_DEFAULT_AUDIO_ENDPOINT_H_
#define FLUTTER_WEBRTC_PLUGIN_DEFAULT_AUDIO_ENDPOINT_H_

#include <string>

namespace flutter_webrtc_plugin {

// Windows only: the endpoint id of the default device for ordinary
// (console) use, or empty when it cannot be read. libwebrtc otherwise opens
// the communications device by role, and Windows answers a communications
// stream by turning every other app down for the whole call.
std::string DefaultConsoleEndpointId(bool capture);

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_DEFAULT_AUDIO_ENDPOINT_H_
