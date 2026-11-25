#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "flutter_deep_filter.h"
#include "flutter_onset_cover.h"
#include "flutter_rate_converter.h"
#include "serial_executor.h"

struct DenoiseState;

namespace flutter_webrtc_plugin {

// Stronger noise suppression with rnnoise, a small recurrent network that
// removes keyboards, fans and room noise that libwebrtc's own suppressor
// lets through. rnnoise only runs at 48 kHz, so any other capture rate is
// resampled up for it and back down; 48 kHz skips both conversions.
//
// "Strongest" adds DeepFilterNet 3, which removes much more of everything
// that is not a steady hum but treats the first words after a quiet spell as
// noise. rnnoise keeps running beside it for the
// speech probability and to cover those onsets, and it is the fallback when
// the model is missing or fails, so a call never has less suppression than
// the member picked.
class NoiseSuppressor {
 public:
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
    // A frame of the model that takes longer than budget_us this many times
    // in a row hands the call to rnnoise for overrun_retry_ms; a failed one
    // or a failed load waits retry_ms. Each repeated overrun doubles the wait,
    // and after max_overrun_retries the model stays off until capture restarts
    // or Strongest is switched off and on.
    int budget_us = 8000;
    int overrun_frames = 30;
    int retry_ms = 10000;
    int overrun_retry_ms = 60000;
    int max_overrun_retries = 3;
    // Stands in for DeepFilter::Load, for tests.
    std::function<std::unique_ptr<DeepFilter>(const DeepFilter::Params&)>
        loader;
  };

  static NoiseSuppressor* Instance();

  // Public so tests can build isolated instances.
  NoiseSuppressor();
  ~NoiseSuppressor();
  NoiseSuppressor(const NoiseSuppressor&) = delete;
  NoiseSuppressor& operator=(const NoiseSuppressor&) = delete;

  void Configure(bool enabled) { enabled_.store(enabled); }
  bool Enabled() const { return enabled_.load(); }

  // Turning Strongest on loads the model on a worker thread, which takes tens
  // of milliseconds, so rnnoise alone runs until a frame finds it ready. The
  // worker also frees it once the audio thread lets go, so neither thread
  // waits on the model. It also makes rnnoise run. Calling it again while on
  // is free.
  void ConfigureStrongest(bool enabled);
  bool StrongestEnabled() const { return strongest_.load(); }

  // Returns once the worker has finished what ConfigureStrongest asked for,
  // for tests and offline tools that need the model from the first frame.
  void WaitForStrongestModel();

  // Set before ConfigureStrongest(true); for tests and tuning.
  void SetStrongestTuning(const StrongestTuning& tuning) { tuning_ = tuning; }

  // Forget the network and resampler history, as when capture restarts, and
  // load a failed model again. Safe on the audio thread.
  void Restart() {
    was_active_ = false;
    overruns_.store(0);
    restart_requested_.store(true);
    worker_.Wake();
  }

  // Whether the last frame was actually denoised.
  bool Active() const { return active_.load(); }

  // Whether DeepFilterNet processed the last frame. False while Strongest is
  // on means rnnoise alone is running.
  bool StrongestActive() const { return strongest_active_.load(); }

  // One 10ms frame of mono samples on the int16 scale, in place. Returns
  // rnnoise's speech probability for it, or kNoSpeechProbability if the
  // frame was left alone.
  float Process(float* buffer, int num_frames);

 private:
  struct Channel;

  bool Prepare(int num_frames);
  float ProcessChannel(Channel& channel, float* buffer, int num_frames,
                       DeepFilter* deep);
  float ProcessStrongest(Channel& channel, DeepFilter& deep, float* out);
  enum class Retire { kSwitchedOff, kFailed, kOverrun };

  void UpdateModel();
  void RetireModel(Retire why);
  int64_t RetryAtMs(Retire why);
  void RequestSettle();
  void Tick();
  void RetryFailedLoad();
  void StartLoad();
  void LoadModel(const StrongestTuning& tuning);
  void Settle();

  std::atomic<bool> enabled_{false};
  std::atomic<bool> active_{false};
  std::atomic<bool> strongest_{false};
  std::atomic<bool> strongest_active_{false};
  // Loaded on the worker, taken over by the audio thread.
  std::atomic<DeepFilter*> pending_{nullptr};
  // Handed back by the audio thread, freed on the worker. A model is only
  // loaded after the last one was settled, so this never holds two.
  std::atomic<DeepFilter*> retired_{nullptr};
  // True from the audio thread taking a model until the worker has settled it.
  std::atomic<bool> model_held_{false};
  std::atomic<bool> model_failed_{false};
  // Steady-clock milliseconds after which a failed model is loaded again.
  std::atomic<int64_t> retry_at_ms_{0};
  std::atomic<bool> settle_requested_{false};
  std::atomic<bool> restart_requested_{false};
  std::atomic<int> overruns_{0};
  std::mutex load_mutex_;
  // Guarded by load_mutex_.
  bool loading_ = false;
  StrongestTuning tuning_;

  // Only touched on the audio thread.
  std::unique_ptr<DeepFilter> model_;
  std::unique_ptr<Channel> channel_;
  int rate_hz_ = 0;
  bool was_active_ = false;

  // Last, so it stops before anything its tasks touch.
  SerialExecutor worker_;
};

}  // namespace flutter_webrtc_plugin

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_NOISE_SUPPRESSOR_H_
