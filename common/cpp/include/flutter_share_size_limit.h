#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_SHARE_SIZE_LIMIT_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_SHARE_SIZE_LIMIT_H_

#include "flutter_frame_fit.h"
#include "flutter_webrtc_base.h"

namespace flutter_webrtc_plugin {

// Puts a size limit between a screen capturer and the track that is published.
//
// libwebrtc's desktop capturer always delivers the screen at its own size and
// has no hook to ask for less, so the frames are scaled here and handed to a
// custom source that the published track reads from. The encoder's own
// adaptation still works on top of it, because that source routes frames
// through libwebrtc's video adapter like a camera does.
class ShareSizeLimiter : public RTCVideoRenderer<scoped_refptr<RTCVideoFrame>> {
 public:
  explicit ShareSizeLimiter(FrameSize limit) : fitter_(limit) {}

  // Reads `capture_track` and returns the track to publish in its place, with
  // `track_id`. Call `Stop` when that track is disposed.
  scoped_refptr<RTCVideoTrack> Start(
      scoped_refptr<RTCPeerConnectionFactory> factory,
      scoped_refptr<RTCVideoTrack> capture_track,
      const std::string& track_id);

  // After this returns no frame is in flight and the capture track, with the
  // capturer behind it, is released.
  void Stop();

  void OnFrame(scoped_refptr<RTCVideoFrame> frame) override;

 private:
  FrameFitter fitter_;
  scoped_refptr<RTCVideoTrack> capture_track_;
  scoped_refptr<RTCVideoSource> output_source_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_SHARE_SIZE_LIMIT_H_
