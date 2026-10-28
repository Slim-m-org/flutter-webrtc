#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_ONSET_COVER_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_ONSET_COVER_H_

namespace flutter_webrtc_plugin {

// Decides, from rnnoise's real-time speech probability, how much of the
// Strongest output should be rnnoise's own instead of DeepFilterNet's, which
// turns down the first words after a quiet spell as if they were noise.
// DeepFilterNet's output trails its input by about three frames, so a run of
// speech frames can be confirmed before the audio it began with has left,
// and a single click or door slam does not count as a word.
class OnsetCover {
 public:
  struct Tuning {
    // rnnoise's speech probability that counts as speech, held for this many
    // 10ms frames in a row before it is a word and not a click.
    float speech_probability = 0.5f;
    int confirm_frames = 5;
    // Frames without speech needed before an onset is covered.
    int quiet_frames = 20;
    // How long rnnoise's output is used from the onset, and the fade back.
    int cover_frames = 20;
    int release_frames = 10;
    // The share of rnnoise's output kept while speech goes on after the
    // cover, so a word DeepFilterNet keeps turning down stays audible.
    float speaking_weight = 0.5f;
  };

  void Reset() { *this = OnsetCover(); }

  // Feeds one frame's probability; returns the weight of rnnoise's output
  // at the end of that frame, 0 to 1.
  float Step(float probability, const Tuning& tuning);

 private:
  // rnnoise's first frames read as speech, so a fresh capture is not quiet.
  static constexpr int kWarmupFrames = 50;
  static constexpr int kQuietCap = 1000;

  int warmup_left_ = kWarmupFrames;
  int run_ = 0;
  int quiet_ = 0;
  int cover_left_ = 0;
  float weight_ = 0.0f;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_ONSET_COVER_H_
