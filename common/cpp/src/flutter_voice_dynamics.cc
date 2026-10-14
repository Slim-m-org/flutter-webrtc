#include "flutter_voice_dynamics.h"

#include <algorithm>
#include <cmath>

namespace flutter_webrtc_plugin {

namespace {

constexpr float kFullScale = 32768.0f;
// Compressor on the RMS level, so it reacts to a shout rather than to the
// peaks of normal speech. The threshold sits above speech that auto gain
// control has levelled (about -18 to -20 dBFS RMS); past it, every 4 dB
// louder comes out 1 dB louder, with a soft knee so the onset is not audible.
constexpr float kThresholdDb = -14.0f;
constexpr float kRatio = 4.0f;
constexpr float kKneeDb = 6.0f;
constexpr float kAttackMs = 10.0f;
constexpr float kReleaseMs = 150.0f;
// Limiter: instant attack, so no sample ever leaves above the ceiling.
constexpr float kCeilingDb = -1.0f;
constexpr float kLimiterReleaseMs = 60.0f;

float Coefficient(float ms, int sample_rate_hz) {
  return 1.0f - std::exp(-1000.0f / (ms * static_cast<float>(sample_rate_hz)));
}

float DbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }

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
}

void VoiceDynamics::Process(float* buffer, int num_frames) {
  const bool enabled = enabled_.load();
  if (buffer == nullptr || num_frames <= 0 || !enabled) {
    was_enabled_ = enabled;
    return;
  }
  const int sample_rate_hz = num_frames * 100;
  if (!was_enabled_ || sample_rate_hz != sample_rate_hz_) {
    Reset(sample_rate_hz);
  }
  was_enabled_ = true;

  const float ceiling = kFullScale * DbToLinear(kCeilingDb);
  for (int i = 0; i < num_frames; ++i) {
    const float normalized = buffer[i] / kFullScale;
    const float power = normalized * normalized;
    const float coeff = power > envelope_ ? attack_coeff_ : release_coeff_;
    envelope_ += (power - envelope_) * coeff;
    const float level_db = 10.0f * std::log10(envelope_ + 1e-12f);
    float sample = buffer[i] * DbToLinear(CompressorGainDb(level_db));

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
}

}  // namespace flutter_webrtc_plugin
