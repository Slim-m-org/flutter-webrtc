#include "flutter_video_renderer.h"

#include <algorithm>
#include <utility>

namespace flutter_webrtc_plugin {

FlutterVideoRenderer::~FlutterVideoRenderer() {}

void FlutterVideoRenderer::initialize(
    TextureRegistrar* registrar,
    BinaryMessenger* messenger,
    TaskRunner* task_runner,
    std::unique_ptr<flutter::TextureVariant> texture,
    int64_t trxture_id) {
  registrar_ = registrar;
  texture_ = std::move(texture);
  texture_id_ = trxture_id;
  std::string channel_name =
      "FlutterWebRTC/Texture" + std::to_string(texture_id_);
  event_channel_ = EventChannelProxy::Create(messenger, task_runner, channel_name);
}

namespace {

// The engine asks for the texture's drawn size, which on desktop is in logical
// pixels; twice that stays sharp at a 2x display scale.
constexpr double kDisplayHeadroom = 2.0;

// The size to convert a frame_width x frame_height frame to for a texture
// drawn at requested_width x requested_height. Only a half or a quarter of the
// frame, the ratios libyuv scales on a fast path: an arbitrary ratio measured
// slower than converting the whole frame. The full frame when the drawn size
// is unknown or needs it.
std::pair<int, int> ConversionSize(int frame_width,
                                   int frame_height,
                                   size_t requested_width,
                                   size_t requested_height) {
  if (requested_width == 0 || requested_height == 0) {
    return {frame_width, frame_height};
  }
  double needed_width = kDisplayHeadroom * requested_width;
  double needed_height = kDisplayHeadroom * requested_height;
  for (int divisor : {4, 2}) {
    if (frame_width / divisor >= needed_width &&
        frame_height / divisor >= needed_height) {
      return {frame_width / divisor, frame_height / divisor};
    }
  }
  return {frame_width, frame_height};
}

}  // namespace

const FlutterDesktopPixelBuffer* FlutterVideoRenderer::CopyPixelBuffer(
    size_t width,
    size_t height) const {
  mutex_.lock();
  if (pixel_buffer_.get() && frame_.get()) {
    // Converting the whole frame on the raster thread is what made a small
    // tile of a high-resolution stream lag, so convert at the drawn size.
    auto [out_width, out_height] =
        ConversionSize(frame_->width(), frame_->height(), width, height);
    size_t buffer_size = size_t(out_width) * size_t(out_height) * (32 >> 3);
    if (buffer_size > rgb_capacity_) {
      rgb_buffer_.reset(new uint8_t[buffer_size]);
      rgb_capacity_ = buffer_size;
    }
    pixel_buffer_->width = out_width;
    pixel_buffer_->height = out_height;

    frame_->ConvertToARGB(RTCVideoFrame::Type::kABGR, rgb_buffer_.get(), 0,
                          out_width, out_height);

    pixel_buffer_->buffer = rgb_buffer_.get();
    mutex_.unlock();
    return pixel_buffer_.get();
  }
  mutex_.unlock();
  return nullptr;
}

void FlutterVideoRenderer::OnFrame(scoped_refptr<RTCVideoFrame> frame) {
  if (!first_frame_rendered) {
    EncodableMap params;
    params[EncodableValue("event")] = "didFirstFrameRendered";
    params[EncodableValue("id")] = EncodableValue(texture_id_);
    event_channel_->Success(EncodableValue(params));
    // Created once and never replaced: CopyPixelBuffer hands the engine a raw
    // pointer to it from the raster thread, so swapping it here after a track
    // switch freed a buffer the engine could still be reading. It sizes the
    // pixels itself on every call.
    mutex_.lock();
    if (!pixel_buffer_) {
      pixel_buffer_ = std::make_shared<FlutterDesktopPixelBuffer>();
      pixel_buffer_->width = 0;
      pixel_buffer_->height = 0;
    }
    mutex_.unlock();
    first_frame_rendered = true;
  }
  if (rotation_ != frame->rotation()) {
    EncodableMap params;
    params[EncodableValue("event")] = "didTextureChangeRotation";
    params[EncodableValue("id")] = EncodableValue(texture_id_);
    params[EncodableValue("rotation")] =
        EncodableValue((int32_t)frame->rotation());
    event_channel_->Success(EncodableValue(params));
    rotation_ = frame->rotation();
  }
  if (last_frame_size_.width != frame->width() ||
      last_frame_size_.height != frame->height()) {
    EncodableMap params;
    params[EncodableValue("event")] = "didTextureChangeVideoSize";
    params[EncodableValue("id")] = EncodableValue(texture_id_);
    params[EncodableValue("width")] = EncodableValue((int32_t)frame->width());
    params[EncodableValue("height")] = EncodableValue((int32_t)frame->height());
    event_channel_->Success(EncodableValue(params));

    last_frame_size_ = {(size_t)frame->width(), (size_t)frame->height()};
  }
  mutex_.lock();
  frame_ = frame;
  mutex_.unlock();
  registrar_->MarkTextureFrameAvailable(texture_id_);
}

void FlutterVideoRenderer::SetVideoTrack(scoped_refptr<RTCVideoTrack> track) {
  if (track_ != track) {
    if (track_)
      track_->RemoveRenderer(this);
    track_ = track;
    last_frame_size_ = {0, 0};
    first_frame_rendered = false;
    if (track_)
      track_->AddRenderer(this);
  }
}

bool FlutterVideoRenderer::CheckMediaStream(std::string mediaId) {
  if (0 == mediaId.size() || 0 == media_stream_id.size()) {
    return false;
  }
  return mediaId == media_stream_id;
}

bool FlutterVideoRenderer::CheckVideoTrack(std::string mediaId) {
  if (0 == mediaId.size() || !track_) {
    return false;
  }
  return mediaId == track_->id().std_string();
}

FlutterVideoRendererManager::FlutterVideoRendererManager(
    FlutterWebRTCBase* base)
    : base_(base) {}

void FlutterVideoRendererManager::CreateVideoRendererTexture(
    std::unique_ptr<MethodResultProxy> result) {
  auto texture = new RefCountedObject<FlutterVideoRenderer>();
  auto textureVariant =
      std::make_unique<flutter::TextureVariant>(flutter::PixelBufferTexture(
          [texture](size_t width,
                    size_t height) -> const FlutterDesktopPixelBuffer* {
            return texture->CopyPixelBuffer(width, height);
          }));

  auto texture_id = base_->textures_->RegisterTexture(textureVariant.get());
  texture->initialize(base_->textures_, base_->messenger_, base_->task_runner_,
                      std::move(textureVariant), texture_id);
  renderers_[texture_id] = texture;
  EncodableMap params;
  params[EncodableValue("textureId")] = EncodableValue(texture_id);
  result->Success(EncodableValue(params));
}

SetSrcObjectStatus FlutterVideoRendererManager::VideoRendererSetSrcObject(
    int64_t texture_id,
    const std::string& stream_id,
    const std::string& owner_tag,
    const std::string& track_id) {
  auto it = renderers_.find(texture_id);
  if (it == renderers_.end()) {
    return SetSrcObjectStatus::kUnknownTexture;
  }
  FlutterVideoRenderer* renderer = it->second.get();

  scoped_refptr<RTCMediaStream> stream =
      base_->MediaStreamForId(stream_id, owner_tag);
  if (!stream.get()) {
    renderer->SetVideoTrack(nullptr);
    renderer->media_stream_id.clear();
    return SetSrcObjectStatus::kOk;
  }

  // The renderer follows the stream even while it has no video, so a track
  // added to it later reaches the renderer.
  renderer->media_stream_id = stream_id;
  auto video_tracks = stream->video_tracks();
  // "0" is what the Dart side sends when the caller named no track.
  const bool any_track = track_id.empty() || track_id == "0";
  scoped_refptr<RTCVideoTrack> chosen;
  for (auto track : video_tracks.std_vector()) {
    if (any_track || track->id().std_string() == track_id) {
      chosen = track;
      break;
    }
  }
  // Never leave the previous track attached to a source that no longer has it.
  renderer->SetVideoTrack(chosen);
  if (!chosen.get() && !any_track) {
    return SetSrcObjectStatus::kTrackNotFound;
  }
  return SetSrcObjectStatus::kOk;
}

void FlutterVideoRendererManager::AttachVideoTrackToStreamRenderers(
    const std::string& stream_id,
    scoped_refptr<RTCVideoTrack> track) {
  for (auto& entry : renderers_) {
    if (entry.second->CheckMediaStream(stream_id)) {
      entry.second->SetVideoTrack(track);
    }
  }
}

void FlutterVideoRendererManager::DetachVideoTrackFromStreamRenderers(
    const std::string& stream_id,
    const std::string& track_id) {
  for (auto& entry : renderers_) {
    if (entry.second->CheckMediaStream(stream_id) &&
        entry.second->CheckVideoTrack(track_id)) {
      entry.second->SetVideoTrack(nullptr);
    }
  }
}

void FlutterVideoRendererManager::VideoRendererDispose(
    int64_t texture_id,
    std::unique_ptr<MethodResultProxy> result) {
  auto it = renderers_.find(texture_id);
  if (it != renderers_.end()) {
    it->second->SetVideoTrack(nullptr);
#if defined(_WINDOWS)
    base_->textures_->UnregisterTexture(texture_id,
                                        [&, it] { renderers_.erase(it); });
#else
    base_->textures_->UnregisterTexture(texture_id);
    renderers_.erase(it);
#endif
    result->Success();
    return;
  }
  result->Error("VideoRendererDisposeFailed",
                "VideoRendererDispose() texture not found!");
}

}  // namespace flutter_webrtc_plugin
