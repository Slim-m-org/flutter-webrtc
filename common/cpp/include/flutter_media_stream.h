#ifndef FLUTTER_WEBRTC_RTC_GET_USERMEDIA_HXX
#define FLUTTER_WEBRTC_RTC_GET_USERMEDIA_HXX

#include <functional>
#include <memory>
#include <vector>

#include "flutter_common.h"
#include "flutter_webrtc_base.h"
#include "serial_executor.h"

namespace flutter_webrtc_plugin {

class FlutterMediaStream {
 public:
  // Writes to the base's track and capturer maps, held back so a camera opened
  // off the platform thread can apply them on it.
  using Commits = std::vector<std::function<void()>>;

  FlutterMediaStream(FlutterWebRTCBase* base);

  void GetUserMedia(const EncodableMap& constraints,
                    std::unique_ptr<MethodResultProxy> result);

  void GetUserAudio(const EncodableMap& constraints,
                    scoped_refptr<RTCMediaStream> stream,
                    EncodableMap& params);

  void GetUserVideo(const EncodableMap& constraints,
                    scoped_refptr<RTCMediaStream> stream,
                    EncodableMap& params,
                    Commits* commits = nullptr);

  void GetSources(std::unique_ptr<MethodResultProxy> result);

  void SelectAudioOutput(const std::string& device_id,
                         std::unique_ptr<MethodResultProxy> result);

  void SelectAudioInput(const std::string& device_id,
                        std::unique_ptr<MethodResultProxy> result);

  void MediaStreamGetTracks(const std::string& stream_id,
                            std::unique_ptr<MethodResultProxy> result);

  void MediaStreamDispose(const std::string& stream_id,
                          std::unique_ptr<MethodResultProxy> result);

  void MediaStreamTrackSetEnable(const std::string& track_id,
                                 std::unique_ptr<MethodResultProxy> result);

  void MediaStreamTrackSwitchCamera(const std::string& track_id,
                                    std::unique_ptr<MethodResultProxy> result);

  void MediaStreamTrackDispose(const std::string& track_id,
                               std::unique_ptr<MethodResultProxy> result);

  void CreateLocalMediaStream(std::unique_ptr<MethodResultProxy> result);

  void OnDeviceChange();

 private:
  FlutterWebRTCBase* base_;
  // Replies queued to the platform thread can outlive this object, so they hold
  // a weak copy of this.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  // Runs one camera open at a time, so two quick toggles do not race the
  // device. Last member, so the open in flight is joined before the rest goes.
  SerialExecutor camera_executor_;
};

}  // namespace flutter_webrtc_plugin

#endif  // !FLUTTER_WEBRTC_RTC_GET_USERMEDIA_HXX
