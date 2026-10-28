#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_

#include <atomic>
#include <memory>
#include <mutex>

#include "flutter_deep_filter.h"
#include "flutter_onset_cover.h"
#include "flutter_rate_converter.h"

struct DenoiseState;

namespace flutter_webrtc_plugin {

// Stronger noise suppression with rnnoise, a small recurrent network that
// removes keyboards, fans and room noise that libwebrtc's own suppressor
// lets through. rnnoise only runs at 48 kHz, so any other capture rate is
// resampled up for it and back down; 48 kHz skips both conversions.
//
// "Strongest" adds DeepFilterNet 3 on the first channel, which removes much
// more of everything that is not a steady hum but treats the first words
// after a quiet spell as noise. rnnoise keeps running beside it for the
// speech probability and to cover those onsets, and it is the fallback when
// the model is missing or fails, so a call never has less suppression than
// the member picked.
class NoiseSuppressor {
 public:
  static constexpr int kMaxChannels = 2;
  // Returned by Process when the frame was not denoised.
  static constexpr float kNoSpeechProbability = -1.0f;

  // How Strongest is set up; the defaults are what ships.
  struct StrongestTuning {
    DeepFilter::Params model = DeepFilter::DefaultParams();
    OnsetCover::Tuning onset;
    // rnnoise's output only stands in for frames louder than this (dBFS),
    // and only where DeepFilterNet is clip_margin_db below it.
    float speech_floor_db = -50.0f;
    float clip_margin_db = 2.0f;
    // False behaves as a build without the model, for tests.
    bool allow_model = true;
  };

  static NoiseSuppressor* Instance();

  // Public so tests can build isolated instances.
  NoiseSuppressor();
  ~NoiseSuppressor();
  NoiseSuppressor(const NoiseSuppressor&) = delete;
  NoiseSuppressor& operator=(const NoiseSuppressor&) = delete;

  void Configure(bool enabled) { enabled_.store(enabled); }
  bool Enabled() const { return enabled_.load(); }

  // Turning Strongest on loads the model on the calling thread, which takes
  // tens of milliseconds, so call it off the audio thread. It also makes
  // rnnoise run. Calling it again while on is free.
  void ConfigureStrongest(bool enabled);
  bool StrongestEnabled() const { return strongest_.load(); }

  // Set before ConfigureStrongest(true); for tests and tuning.
  void SetStrongestTuning(const StrongestTuning& tuning) { tuning_ = tuning; }

  // Forget the network and resampler history, as when capture restarts.
  void Restart() { was_active_ = false; }

  // Whether the last frame was actually denoised.
  bool Active() const { return active_.load(); }

  // Whether DeepFilterNet processed the last frame. False while Strongest is
  // on means rnnoise alone is running.
  bool StrongestActive() const { return strongest_active_.load(); }

  // One 10ms frame of mono samples on the int16 scale, in place. Returns
  // rnnoise's speech probability for it, or kNoSpeechProbability if the
  // frame was left alone.
  float Process(float* buffer, int num_frames);

  // The same for one planar frame per channel; returns the loudest-speech
  // channel's probability.
  float ProcessPlanar(float* const* channels, int num_channels,
                      int num_frames);

 private:
  struct Channel;

  bool Prepare(int num_channels, int num_frames);
  float ProcessChannel(Channel& channel, float* buffer, int num_frames,
                       DeepFilter* deep);
  float ProcessStrongest(Channel& channel, DeepFilter& deep, float* out);
  void UpdateModel();
  void DropModel(bool failed);

  std::atomic<bool> enabled_{false};
  std::atomic<bool> active_{false};
  std::atomic<bool> strongest_{false};
  std::atomic<bool> strongest_active_{false};
  // Loaded on the platform thread, taken over by the audio thread.
  std::atomic<DeepFilter*> pending_{nullptr};
  std::atomic<bool> model_held_{false};
  std::atomic<bool> model_failed_{false};
  std::mutex load_mutex_;
  StrongestTuning tuning_;

  // Only touched on the audio thread.
  std::unique_ptr<DeepFilter> model_;
  std::unique_ptr<Channel> channels_[kMaxChannels];
  int rate_hz_ = 0;
  bool was_active_ = false;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
