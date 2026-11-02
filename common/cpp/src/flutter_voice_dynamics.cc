#include "flutter_voice_dynamics.h"

#include <algorithm>
#include <cmath>

namespace flutter_webrtc_plugin {

namespace {

constexpr float kFullScale = 32768.0f;
// Compressor on the RMS level, so it reacts to a shout rather than to the
// peaks of normal speech. The threshold sits above levelled speech (about
// -18 to -20 dBFS RMS); past it, every 4 dB louder comes out 1 dB louder,
// with a soft knee so the onset is not audible.
constexpr float kThresholdDb = -14.0f;
constexpr float kRatio = 4.0f;
constexpr float kKneeDb = 6.0f;
constexpr float kAttackMs = 10.0f;
constexpr float kReleaseMs = 150.0f;
// Limiter: instant attack, so no sample ever leaves above the ceiling.
constexpr float kCeilingDb = -1.0f;
constexpr float kLimiterReleaseMs = 60.0f;
// Below any audible level (-200 dB); snapping to zero keeps the decaying
// envelope out of the denormal range, where float math is slow.
constexpr float kEnvelopeFloor = 1e-20f;
// Leveler: where speech should sit, how far it may be lifted, and how sure
// the suppressor must be before a frame counts. Five confident frames in a
// row (50 ms) rule out a click or a crunch that the suppressor read as
// speech for a moment; a word is longer than that.
constexpr float kLevelerTargetDb = -20.0f;
constexpr float kLevelerMaxDb = 12.0f;
constexpr float kLevelerSpeech = 0.8f;
constexpr float kLevelerFloorDb = -50.0f;
constexpr int kLevelerConfirmFrames = 5;
// Up slowly, so a lift is never heard as pumping; down faster, so a shout
// does not stay loud.
constexpr float kLevelerUpDbPerFrame = 0.02f;
constexpr float kLevelerDownDbPerFrame = 0.1f;

float Coefficient(float ms, int sample_rate_hz) {
  return 1.0f - std::exp(-1000.0f / (ms * static_cast<float>(sample_rate_hz)));
}

float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

float FrameRmsDb(const float* buffer, int num_frames) {
  double sum = 0.0;
  for (int i = 0; i < num_frames; ++i) {
    const double v = buffer[i] / kFullScale;
    sum += v * v;
  }
  return 10.0f * std::log10(static_cast<float>(sum / num_frames) + 1e-12f);
}

// Gain reduction in dB (zero or negative) for a detected level.
float CompressorGainDb(float level_db) {
  const float over = level_db - kThresholdDb;
  if (2.0f * over <= -kKneeDb) return 0.0f;
  const float slope = 1.0f / kRatio - 1.0f;
  if (2.0f * over < kKneeDb) {
    const float into_knee = over + kKneeDb / 2.0f;
    return slope * into_knee * into_knee / (2.0f * kKneeDb);
  }
  return slope * over;
}

}  // namespace

VoiceDynamics* VoiceDynamics::Instance() {
  static VoiceDynamics* dynamics = new VoiceDynamics();
  return dynamics;
}

void VoiceDynamics::Reset(int sample_rate_hz) {
  sample_rate_hz_ = sample_rate_hz;
  attack_coeff_ = Coefficient(kAttackMs, sample_rate_hz);
  release_coeff_ = Coefficient(kReleaseMs, sample_rate_hz);
  limiter_release_coeff_ = Coefficient(kLimiterReleaseMs, sample_rate_hz);
  envelope_ = 0.0f;
  limiter_gain_ = 1.0f;
  gain_db_ = 0.0f;
  applied_gain_ = 1.0f;
  speech_run_ = 0;
  leveler_gain_db_.store(0.0f);
}

void VoiceDynamics::AdaptLeveler(const float* buffer, int num_frames,
                                 float speech_probability) {
  const float level_db = FrameRmsDb(buffer, num_frames);
  const bool speech = speech_probability >= kLevelerSpeech &&
                      level_db + gain_db_ > kLevelerFloorDb;
  speech_run_ = speech ? speech_run_ + 1 : 0;
  if (speech_run_ < kLevelerConfirmFrames) return;
  const float error = kLevelerTargetDb - (level_db + gain_db_);
  const float step =
      std::clamp(error, -kLevelerDownDbPerFrame, kLevelerUpDbPerFrame);
  gain_db_ = std::clamp(gain_db_ + step, 0.0f, kLevelerMaxDb);
}

void VoiceDynamics::Process(float* buffer, int num_frames,
                            float speech_probability) {
  const bool enabled = enabled_.load();
  const bool leveler = leveler_.load();
  if (buffer == nullptr || num_frames <= 0 || (!enabled && !leveler)) {
    was_enabled_ = enabled || leveler;
    leveler_gain_db_.store(0.0f);
    return;
  }
  const int sample_rate_hz = num_frames * 100;
  if (!was_enabled_ || sample_rate_hz != sample_rate_hz_) {
    Reset(sample_rate_hz);
  }
  was_enabled_ = true;

  if (leveler) {
    AdaptLeveler(buffer, num_frames, speech_probability);
  } else {
    gain_db_ = 0.0f;
    speech_run_ = 0;
  }
  leveler_gain_db_.store(gain_db_);
  const float target_gain = DbToLinear(gain_db_);
  const float gain_step = (target_gain - applied_gain_) / num_frames;

  const float ceiling = kFullScale * DbToLinear(kCeilingDb);
  for (int i = 0; i < num_frames; ++i) {
    applied_gain_ += gain_step;
    float sample = buffer[i] * applied_gain_;
    if (enabled) {
      const float normalized = sample / kFullScale;
      const float power = normalized * normalized;
      const float coeff = power > envelope_ ? attack_coeff_ : release_coeff_;
      envelope_ += (power - envelope_) * coeff;
      if (envelope_ < kEnvelopeFloor) envelope_ = 0.0f;
      const float level_db = 10.0f * std::log10(envelope_ + 1e-12f);
      sample *= DbToLinear(CompressorGainDb(level_db));
    }

    const float peak = std::fabs(sample);
    const float needed = peak > ceiling ? ceiling / peak : 1.0f;
    if (needed < limiter_gain_) {
      limiter_gain_ = needed;
    } else {
      limiter_gain_ += (1.0f - limiter_gain_) * limiter_release_coeff_;
    }
    sample *= limiter_gain_;
    buffer[i] = std::clamp(sample, -ceiling, ceiling);
  }
  applied_gain_ = target_gain;
}

}  // namespace flutter_webrtc_plugin
