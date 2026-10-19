#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

#include "check.h"
#include "flutter_input_gate.h"

namespace {

using flutter_webrtc_plugin::InputGate;

constexpr int kRate = 48000;
constexpr int kFrame = kRate / 100;
constexpr float kThresholdDb = -50.0f;

float Amplitude(float db) { return 32768.0f * std::pow(10.0f, db / 20.0f); }

// A deterministic noise frame at the given RMS level.
std::vector<float> Noise(float db, unsigned* seed) {
  std::vector<float> f(kFrame);
  for (float& v : f) {
    *seed = *seed * 1664525u + 1013904223u;
    const float u = static_cast<float>(*seed >> 8) / 8388608.0f - 1.0f;
    v = u * Amplitude(db) * 1.7320508f;
  }
  return f;
}

std::vector<float> Tone(float db) {
  std::vector<float> f(kFrame);
  for (int i = 0; i < kFrame; ++i) {
    f[i] = Amplitude(db) * 1.41421356f *
           static_cast<float>(std::sin(2.0 * M_PI * 200.0 * i / kRate));
  }
  return f;
}

double Energy(const std::vector<float>& f) {
  double e = 0.0;
  for (float v : f) e += static_cast<double>(v) * v;
  return e;
}

void Prepare(InputGate& gate, bool enabled) {
  gate.Configure(enabled, kThresholdDb);
  gate.Initialize(kRate, 1);
}

// How many frames the gate holds the audio back, measured with a step.
int MeasuredDelayFrames(InputGate& gate) {
  Prepare(gate, false);
  for (int n = 0; n < 10; ++n) {
    std::vector<float> f(kFrame, n == 0 ? 1000.0f : 0.0f);
    gate.ProcessWithSpeech(kFrame, f.data(), -1.0f);
    if (Energy(f) > 0.0) return n;
  }
  return -1;
}

}  // namespace

TEST(gate_lookahead_keeps_a_soft_onset_before_the_vowel) {
  InputGate& gate = *InputGate::Instance();
  const int delay = MeasuredDelayFrames(gate);
  Prepare(gate, true);
  unsigned seed = 1;
  std::vector<std::vector<float>> in, out;
  auto feed = [&](std::vector<float> f, float speech) {
    in.push_back(f);
    gate.ProcessWithSpeech(kFrame, f.data(), speech);
    out.push_back(f);
  };
  for (int i = 0; i < 40; ++i) feed(Noise(-80.0f, &seed), 0.0f);
  const size_t fricative = in.size();
  // A fricative under the loud margin and below the speech call, then a vowel.
  feed(Noise(-45.0f, &seed), 0.2f);
  feed(Noise(-45.0f, &seed), 0.2f);
  feed(Tone(-25.0f), 0.95f);
  feed(Tone(-25.0f), 0.95f);
  feed(Tone(-25.0f), 0.95f);

  // Output frame k is input frame k-delay, so the two fricative frames come
  // out at calls fricative+delay and fricative+delay+1.
  const size_t vowel = fricative + 2;
  const size_t at = fricative + delay;
  const double kept = Energy(out[at]) + Energy(out[at + 1]);
  const double wanted = Energy(in[fricative]) + Energy(in[fricative + 1]);
  std::printf("  soft onset kept %.1f%% of the 20 ms before the vowel\n",
              100.0 * kept / wanted);
  CHECK(kept > 0.8 * wanted);
  CHECK(Energy(out[vowel + delay]) > 0.95 * Energy(in[vowel]));
}

TEST(gate_delays_the_audio_by_exactly_two_frames) {
  InputGate& gate = *InputGate::Instance();
  Prepare(gate, false);
  std::vector<std::vector<float>> in, out;
  for (int n = 0; n < 6; ++n) {
    std::vector<float> f(kFrame, static_cast<float>(100 * (n + 1)));
    in.push_back(f);
    gate.ProcessWithSpeech(kFrame, f.data(), -1.0f);
    out.push_back(f);
  }
  CHECK(Energy(out[0]) == 0.0);
  CHECK(Energy(out[1]) == 0.0);
  for (int n = 2; n < 6; ++n) CHECK(out[n] == in[n - 2]);
}

TEST(gate_restart_does_not_replay_the_old_delay_line) {
  InputGate& gate = *InputGate::Instance();
  Prepare(gate, false);
  for (int i = 0; i < 10; ++i) {
    std::vector<float> f = Tone(-10.0f);
    gate.ProcessWithSpeech(kFrame, f.data(), -1.0f);
  }
  gate.Reset(kRate);
  for (int i = 0; i < 2; ++i) {
    std::vector<float> f(kFrame, 0.0f);
    gate.ProcessWithSpeech(kFrame, f.data(), -1.0f);
    CHECK(Energy(f) == 0.0);
  }
}

TEST(gate_frame_size_change_does_not_replay_the_old_delay_line) {
  InputGate& gate = *InputGate::Instance();
  Prepare(gate, false);
  for (int i = 0; i < 10; ++i) {
    std::vector<float> f = Tone(-10.0f);
    gate.ProcessWithSpeech(kFrame, f.data(), -1.0f);
  }
  for (int i = 0; i < 4; ++i) {
    std::vector<float> f(441, 0.0f);
    gate.ProcessWithSpeech(441, f.data(), -1.0f);
    CHECK(Energy(f) == 0.0);
  }
}

TEST(gate_hysteresis_keeps_a_fading_word_open) {
  InputGate& gate = *InputGate::Instance();
  Prepare(gate, true);
  unsigned seed = 3;
  auto feed = [&](float db) {
    std::vector<float> f = Noise(db, &seed);
    gate.ProcessWithSpeech(kFrame, f.data(), -1.0f);
    return gate.VoiceDetected();
  };
  CHECK(feed(-40.0f));
  // Two dB under the threshold is inside the hysteresis: still counts.
  for (int i = 0; i < 60; ++i) CHECK(feed(-52.0f));

  Prepare(gate, true);
  CHECK(feed(-40.0f));
  int closed_after = 0;
  for (int i = 1; i <= 60 && closed_after == 0; ++i) {
    if (!feed(-56.0f)) closed_after = i;
  }
  CHECK(closed_after == 31);
}

TEST(gate_hysteresis_does_not_open_below_the_threshold) {
  InputGate& gate = *InputGate::Instance();
  Prepare(gate, true);
  unsigned seed = 5;
  for (int i = 0; i < 30; ++i) {
    std::vector<float> f = Noise(-52.0f, &seed);
    gate.ProcessWithSpeech(kFrame, f.data(), 0.95f);
    CHECK(!gate.VoiceDetected());
  }
}

TEST(gate_lookahead_cost) {
  InputGate& gate = *InputGate::Instance();
  Prepare(gate, true);
  std::vector<float> f = Tone(-30.0f);
  const int frames = 20000;
  const clock_t start = clock();
  for (int i = 0; i < frames; ++i) {
    std::vector<float> g = f;
    gate.ProcessWithSpeech(kFrame, g.data(), 0.9f);
  }
  const double us = 1e6 * (clock() - start) / CLOCKS_PER_SEC / frames;
  std::printf("  gate: %.2f us per 10 ms frame at 48 kHz\n", us);
  CHECK(us < 100.0);
}
