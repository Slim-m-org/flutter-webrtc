#ifndef PULSE_LOOPBACK_CAPTURER_H_
#define PULSE_LOOPBACK_CAPTURER_H_

#ifdef __linux__

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include <pulse/pulseaudio.h>

#include "loopback_capturer.h"

namespace flutter_webrtc_plugin {

// Linux PulseAudio / PipeWire (pulse compat) LoopbackCapturer.
//
// Records the default sink's monitor on a pa_threaded_mainloop, so stopping
// never waits on a blocked read, and follows the default sink when it changes.
// The capture stream asks for exactly 48 kHz stereo s16le, so the server
// converts and each 10 ms of data is one CaptureFrame.
class PulseLoopbackCapturer : public LoopbackCapturer {
 public:
  explicit PulseLoopbackCapturer(const std::string& source_id);
  ~PulseLoopbackCapturer() override;

  bool Start(scoped_refptr<RTCAudioSource> source) override;
  void Stop() override;

  // False once the audio session broke or the capturer was stopped.
  bool IsRunning() const { return running_; }

 private:
  static void OnContextState(pa_context* context, void* userdata);
  static void OnSubscribe(pa_context* context,
                          pa_subscription_event_type_t event, uint32_t index,
                          void* userdata);
  static void OnServerInfo(pa_context* context, const pa_server_info* info,
                           void* userdata);
  static void OnSinkInfo(pa_context* context, const pa_sink_info* info, int eol,
                         void* userdata);
  static void OnStreamState(pa_stream* stream, void* userdata);
  static void OnStreamRead(pa_stream* stream, size_t bytes, void* userdata);

  // The methods below run on the mainloop thread, or under its lock.
  void FollowDefaultSink();
  void ConnectStream(const std::string& monitor);
  void DropStream();
  void Feed(const uint8_t* data, size_t bytes);
  void Fail(const char* reason);

  // Desktop source id from getDisplayMedia; the whole default monitor is
  // captured on Linux, so it is kept for interface parity only.
  std::string source_id_;
  scoped_refptr<RTCAudioSource> source_;

  pa_threaded_mainloop* mainloop_ = nullptr;
  pa_context* context_ = nullptr;
  pa_stream* stream_ = nullptr;

  // A named monitor from SLIMM_SHARE_AUDIO_SOURCE is never retargeted.
  bool pinned_ = false;
  std::string connected_monitor_;
  int failed_connects_ = 0;
  std::vector<uint8_t> pending_;

  std::atomic<bool> running_{false};
  std::atomic<bool> stream_ready_{false};
  std::atomic<bool> failed_{false};
  bool was_running_ = false;
};

}  // namespace flutter_webrtc_plugin

#endif  // __linux__
#endif  // PULSE_LOOPBACK_CAPTURER_H_
