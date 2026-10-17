#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_RATE_CONVERTER_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_RATE_CONVERTER_H_

#include <vector>

namespace flutter_webrtc_plugin {

// Streaming windowed-sinc resampler for whole 10 ms frames between two rates
// that are multiples of 100 Hz. Configure allocates; Convert does not.
class RateConverter {
 public:
  bool Configure(int in_rate_hz, int out_rate_hz);
  bool Configured() const { return !kernels_.empty(); }
  int InFrames() const { return in_frames_; }
  int OutFrames() const { return out_frames_; }

  // Latency this stage adds, in input samples.
  int DelaySamples() const { return half_taps_; }

  void Reset();

  // in has InFrames() samples, out has room for OutFrames().
  void Convert(const float* in, float* out);

 private:
  int taps_ = 0;
  int half_taps_ = 0;
  int in_frames_ = 0;
  int out_frames_ = 0;
  // Per output sample of a frame: where its newest tap sits and its phase.
  std::vector<int> base_;
  std::vector<int> phase_;
  std::vector<float> kernels_;
  std::vector<float> scratch_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_RATE_CONVERTER_H_
