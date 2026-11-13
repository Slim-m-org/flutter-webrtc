#ifndef FLUTTER_WEBRTC_AUDIO_SERVER_PROBE_H_
#define FLUTTER_WEBRTC_AUDIO_SERVER_PROBE_H_

namespace flutter_webrtc_plugin {

// libwebrtc aborts the process when its audio device module cannot start, so
// the factory is only created once the platform audio server answers. Only
// Linux can tell; elsewhere, and in a build without libpulse, this is true.
#ifdef __linux__
bool AudioServerReachable();
#else
inline bool AudioServerReachable() { return true; }
#endif

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_AUDIO_SERVER_PROBE_H_
