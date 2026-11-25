// Linux system-audio (loopback) capture for flutter_webrtc.
//
// Mirrors windows/application_loopback_capturer.cc but records the default
// sink's monitor through libpulse. Compiled only when the plugin build found
// libpulse (HAVE_LIBPULSE, set in linux/CMakeLists.txt); otherwise this
// translation unit is empty and CreateLoopbackCapturer returns nullptr, so
// getDisplayMedia continues without system audio.
//
// PipeWire ships a PulseAudio compatibility layer, so one libpulse backend
// covers both stacks.

#if defined(__linux__) && defined(HAVE_LIBPULSE)

#include "pulse_loopback_capturer.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace flutter_webrtc_plugin {

namespace {

// Frame contract fed to RTCAudioSource::CaptureFrame, matching the Windows
// capturer: 16-bit PCM, 48 kHz, 2 channels, 480 samples per channel = 10 ms.
constexpr int kBitsPerSample = 16;
constexpr int kSampleRate = 48000;
constexpr size_t kChannels = 2;
constexpr size_t kFramesPer10ms = kSampleRate / 100;
constexpr size_t kFrameBytes = kFramesPer10ms * kChannels * sizeof(int16_t);

constexpr auto kStartTimeout = std::chrono::seconds(3);

// A monitor that cannot be connected this many times in a row ends the capture.
constexpr int kMaxFailedConnects = 5;

}  // namespace

PulseLoopbackCapturer::PulseLoopbackCapturer(const std::string& source_id)
    : source_id_(source_id) {}

PulseLoopbackCapturer::~PulseLoopbackCapturer() {
  Stop();
}

bool PulseLoopbackCapturer::Start(scoped_refptr<RTCAudioSource> source) {
  if (running_) return true;
  Stop();

  source_ = source;
  failed_ = false;
  was_running_ = false;
  stream_ready_ = false;
  failed_connects_ = 0;
  connected_monitor_.clear();

  // A caller that built its own mix names it here; otherwise the default sink's monitor.
  const char* chosen = std::getenv("SLIMM_SHARE_AUDIO_SOURCE");
  pinned_ = chosen && *chosen;

  mainloop_ = pa_threaded_mainloop_new();
  if (!mainloop_) {
    source_ = nullptr;
    return false;
  }
  context_ = pa_context_new(pa_threaded_mainloop_get_api(mainloop_),
                            "flutter_webrtc");
  if (!context_) {
    Stop();
    return false;
  }
  pa_context_set_state_callback(context_, &OnContextState, this);

  pa_threaded_mainloop_lock(mainloop_);
  const bool started =
      pa_threaded_mainloop_start(mainloop_) >= 0 &&
      pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0;
  if (started && pinned_) {
    // The context is not ready yet; the pinned monitor connects in OnContextState.
    connected_monitor_ = chosen;
  }
  pa_threaded_mainloop_unlock(mainloop_);

  const auto deadline = std::chrono::steady_clock::now() + kStartTimeout;
  while (started && !stream_ready_ && !failed_ &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!started || !stream_ready_ || failed_) {
    std::cerr << "[LoopbackCapturer] Could not open the default sink monitor; "
                 "system-audio capture unavailable.\n";
    Stop();
    return false;
  }
  return true;
}

void PulseLoopbackCapturer::Stop() {
  if (!mainloop_) return;
  running_ = false;

  pa_threaded_mainloop_lock(mainloop_);
  DropStream();
  if (context_) {
    pa_context_set_state_callback(context_, nullptr, nullptr);
    pa_context_set_subscribe_callback(context_, nullptr, nullptr);
    pa_context_disconnect(context_);
    pa_context_unref(context_);
    context_ = nullptr;
  }
  pa_threaded_mainloop_unlock(mainloop_);

  pa_threaded_mainloop_stop(mainloop_);
  pa_threaded_mainloop_free(mainloop_);
  mainloop_ = nullptr;
  source_ = nullptr;
  pending_.clear();
}

void PulseLoopbackCapturer::OnContextState(pa_context* context,
                                           void* userdata) {
  auto* self = static_cast<PulseLoopbackCapturer*>(userdata);
  switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY: {
      pa_context_set_subscribe_callback(context, &OnSubscribe, self);
      pa_operation* op = pa_context_subscribe(
          context,
          static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SERVER |
                                              PA_SUBSCRIPTION_MASK_SINK),
          nullptr, nullptr);
      if (op) pa_operation_unref(op);
      if (self->pinned_) {
        self->ConnectStream(self->connected_monitor_);
      } else {
        self->FollowDefaultSink();
      }
      break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
      self->Fail("the audio server connection ended");
      break;
    default:
      break;
  }
}

void PulseLoopbackCapturer::OnSubscribe(pa_context* /*context*/,
                                        pa_subscription_event_type_t event,
                                        uint32_t /*index*/, void* userdata) {
  auto* self = static_cast<PulseLoopbackCapturer*>(userdata);
  if (self->pinned_ || self->failed_) return;
  const auto facility = event & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  const auto type = event & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
  const bool server_changed =
      facility == PA_SUBSCRIPTION_EVENT_SERVER &&
      type == PA_SUBSCRIPTION_EVENT_CHANGE;
  const bool sink_set_changed =
      facility == PA_SUBSCRIPTION_EVENT_SINK &&
      (type == PA_SUBSCRIPTION_EVENT_NEW ||
       type == PA_SUBSCRIPTION_EVENT_REMOVE);
  if (server_changed || sink_set_changed) {
    self->FollowDefaultSink();
  }
}

// Asks the server which sink is the default, then connects to its monitor
// unless the stream already records it.
void PulseLoopbackCapturer::FollowDefaultSink() {
  if (!context_) return;
  pa_operation* op =
      pa_context_get_server_info(context_, &OnServerInfo, this);
  if (op) {
    pa_operation_unref(op);
  } else if (!stream_) {
    Fail("the default sink could not be queried");
  }
}

void PulseLoopbackCapturer::OnServerInfo(pa_context* context,
                                         const pa_server_info* info,
                                         void* userdata) {
  auto* self = static_cast<PulseLoopbackCapturer*>(userdata);
  if (!info || !info->default_sink_name || !*info->default_sink_name) {
    if (!self->stream_) self->Fail("there is no default sink");
    return;
  }
  pa_operation* op = pa_context_get_sink_info_by_name(
      context, info->default_sink_name, &OnSinkInfo, self);
  if (op) {
    pa_operation_unref(op);
  } else if (!self->stream_) {
    self->Fail("the default sink could not be queried");
  }
}

void PulseLoopbackCapturer::OnSinkInfo(pa_context* /*context*/,
                                       const pa_sink_info* info, int eol,
                                       void* userdata) {
  auto* self = static_cast<PulseLoopbackCapturer*>(userdata);
  if (eol < 0) {
    if (!self->stream_) self->Fail("the default sink is gone");
    return;
  }
  if (eol > 0 || !info || !info->monitor_source_name) return;
  if (self->stream_ && self->connected_monitor_ == info->monitor_source_name) {
    return;
  }
  self->ConnectStream(info->monitor_source_name);
}

void PulseLoopbackCapturer::ConnectStream(const std::string& monitor) {
  DropStream();
  connected_monitor_ = monitor;

  const pa_sample_spec spec = {PA_SAMPLE_S16LE, kSampleRate,
                               static_cast<uint8_t>(kChannels)};
  stream_ = pa_stream_new(context_, "System audio loopback", &spec, nullptr);
  if (!stream_) {
    Fail("the capture stream could not be created");
    return;
  }
  pa_stream_set_state_callback(stream_, &OnStreamState, this);
  pa_stream_set_read_callback(stream_, &OnStreamRead, this);

  // 10 ms fragments; the other attributes stay at the server default.
  pa_buffer_attr attr;
  attr.maxlength = static_cast<uint32_t>(-1);
  attr.tlength = static_cast<uint32_t>(-1);
  attr.prebuf = static_cast<uint32_t>(-1);
  attr.minreq = static_cast<uint32_t>(-1);
  attr.fragsize = static_cast<uint32_t>(kFrameBytes);

  // The server must not move the stream; this capturer retargets it itself.
  if (pa_stream_connect_record(stream_, monitor.c_str(), &attr,
                               PA_STREAM_DONT_MOVE) < 0) {
    DropStream();
    Fail("the capture stream could not connect");
  }
}

void PulseLoopbackCapturer::DropStream() {
  if (!stream_) return;
  pa_stream_set_state_callback(stream_, nullptr, nullptr);
  pa_stream_set_read_callback(stream_, nullptr, nullptr);
  const pa_stream_state_t state = pa_stream_get_state(stream_);
  if (state == PA_STREAM_READY || state == PA_STREAM_CREATING) {
    pa_stream_disconnect(stream_);
  }
  pa_stream_unref(stream_);
  stream_ = nullptr;
  stream_ready_ = false;
  pending_.clear();
}

void PulseLoopbackCapturer::OnStreamState(pa_stream* stream, void* userdata) {
  auto* self = static_cast<PulseLoopbackCapturer*>(userdata);
  switch (pa_stream_get_state(stream)) {
    case PA_STREAM_READY:
      self->failed_connects_ = 0;
      self->stream_ready_ = true;
      self->running_ = true;
      self->was_running_ = true;
      break;
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
      // The monitor went away, for instance with its sink.
      self->DropStream();
      if (self->pinned_ || ++self->failed_connects_ > kMaxFailedConnects) {
        self->Fail("the capture stream ended");
      } else {
        self->FollowDefaultSink();
      }
      break;
    default:
      break;
  }
}

void PulseLoopbackCapturer::OnStreamRead(pa_stream* stream, size_t /*bytes*/,
                                         void* userdata) {
  auto* self = static_cast<PulseLoopbackCapturer*>(userdata);
  while (pa_stream_readable_size(stream) > 0) {
    const void* data = nullptr;
    size_t bytes = 0;
    if (pa_stream_peek(stream, &data, &bytes) < 0) {
      self->Fail("the capture stream could not be read");
      return;
    }
    if (bytes == 0) break;
    if (data) self->Feed(static_cast<const uint8_t*>(data), bytes);
    pa_stream_drop(stream);
  }
}

// Each call hands on whole 10 ms frames and keeps the remainder for the next.
void PulseLoopbackCapturer::Feed(const uint8_t* data, size_t bytes) {
  pending_.insert(pending_.end(), data, data + bytes);
  size_t offset = 0;
  while (source_ && pending_.size() - offset >= kFrameBytes) {
    source_->CaptureFrame(pending_.data() + offset, kBitsPerSample,
                          kSampleRate, kChannels, kFramesPer10ms);
    offset += kFrameBytes;
  }
  pending_.erase(pending_.begin(), pending_.begin() + offset);
}

void PulseLoopbackCapturer::Fail(const char* reason) {
  if (failed_.exchange(true)) return;
  running_ = false;
  std::cerr << "[LoopbackCapturer] Capture ended: " << reason << ".\n";
  // A capture that never started is reported by Start returning false.
  if (was_running_) ReportError();
}

}  // namespace flutter_webrtc_plugin

#endif  // __linux__ && HAVE_LIBPULSE
