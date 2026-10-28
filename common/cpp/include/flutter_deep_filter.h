#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_DEEP_FILTER_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_DEEP_FILTER_H_

#include <memory>

namespace flutter_webrtc_plugin {

// DeepFilterNet 3 (MIT or Apache-2.0) run through tract, from the Rust
// library in third_party/dfn. It works on whole 10ms frames of mono 48 kHz
// audio and holds about 48 MB, so it is only loaded when asked for. A build
// without the Rust library compiles this to a class that never loads.
class DeepFilter {
 public:
  static constexpr int kFrameSize = 480;

  // Model settings; NaN keeps the model's default.
  struct Params {
    // Most a frame may be turned down, in dB.
    float atten_lim_db;
    // Local SNR below which a frame is zeroed, and above which the first or
    // second stage is skipped.
    float min_db;
    float erb_db;
    float df_db;
  };

  static Params DefaultParams();

  // Whether this build links the model at all.
  static bool Compiled();

  // Loads the model, which takes tens of milliseconds, so never on the audio
  // thread. Null when this build has none or it cannot run.
  static std::unique_ptr<DeepFilter> Load(const Params& params);

  ~DeepFilter();
  DeepFilter(const DeepFilter&) = delete;
  DeepFilter& operator=(const DeepFilter&) = delete;

  // One frame on the int16 scale. False, with out untouched, when the model
  // failed or produced a non-finite sample.
  bool Process(const float* in, float* out);

 private:
  struct Impl;
  explicit DeepFilter(Impl* impl);

  std::unique_ptr<Impl> impl_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_DEEP_FILTER_H_
