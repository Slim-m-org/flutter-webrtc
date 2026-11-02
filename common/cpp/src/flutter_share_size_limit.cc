#include "flutter_share_size_limit.h"

namespace flutter_webrtc_plugin {

scoped_refptr<RTCVideoTrack> ShareSizeLimiter::Start(
    scoped_refptr<RTCPeerConnectionFactory> factory,
    scoped_refptr<RTCVideoTrack> capture_track,
    const std::string& track_id) {
  capture_track_ = capture_track;
  output_source_ = factory->CreateCustomVideoSource(
      "screen_capture_output", RTCMediaConstraints::Create());
  scoped_refptr<RTCVideoTrack> output =
      factory->CreateVideoTrack(output_source_, track_id.c_str());
  capture_track_->AddRenderer(this);
  return output;
}

void ShareSizeLimiter::Stop() {
  if (capture_track_.get()) {
    // Returns once the capture thread has left OnFrame.
    capture_track_->RemoveRenderer(this);
  }
  capture_track_ = nullptr;
  output_source_ = nullptr;
}

void ShareSizeLimiter::OnFrame(scoped_refptr<RTCVideoFrame> frame) {
  const I420View in{frame->width(),   frame->height(), frame->DataY(),
                    frame->StrideY(), frame->DataU(),  frame->StrideU(),
                    frame->DataV(),   frame->StrideV()};
  const ScaledI420* scaled = fitter_.Fit(in);
  if (!scaled) {
    output_source_->OnCapturedFrame(frame);
    return;
  }
  output_source_->OnCapturedFrame(RTCVideoFrame::Create(
      scaled->width, scaled->height, scaled->y.data(), scaled->stride_y(),
      scaled->u.data(), scaled->stride_uv(), scaled->v.data(),
      scaled->stride_uv()));
}

}  // namespace flutter_webrtc_plugin
