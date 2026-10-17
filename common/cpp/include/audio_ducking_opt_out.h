#ifndef FLUTTER_WEBRTC_PLUGIN_AUDIO_DUCKING_OPT_OUT_H_
#define FLUTTER_WEBRTC_PLUGIN_AUDIO_DUCKING_OPT_OUT_H_

namespace flutter_webrtc_plugin {

#ifdef _WIN32
// Windows only: opts this process's default audio session out of the system
// ducking experience on every active capture and render endpoint, so joining
// a call never turns other apps down. Safe to call repeatedly and from any
// thread; failures are logged and otherwise ignored.
void OptOutOfAudioDucking();
#else
inline void OptOutOfAudioDucking() {}
#endif

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_AUDIO_DUCKING_OPT_OUT_H_
