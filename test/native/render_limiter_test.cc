#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.h"
#include "flutter_render_gains.h"
#include "flutter_render_limiter.h"

namespace {

using flutter_webrtc_plugin::RenderGains;
using flutter_webrtc_plugin::RenderLimiter;

constexpr int kRate = 48000;
constexpr float kFullScale = 32767.0f;

std::vector<float> Sine(float peak, int count, float hz = 997.0f) {
  std::vector<float> out(count);
  for (int i = 0; i < count; ++i) {
    out[i] = peak * std::sin(2.0f * 3.14159265f * hz * i / kRate);
  }
  return out;
}

float Peak(const std::vector<float>& v) {
  float p = 0;
  for (float x : v) p = std::max(p, std::fabs(x));
  return p;
}

float Rms(const std::vector<float>& v) {
  double sum = 0;
  for (float x : v) sum += static_cast<double>(x) * x;
  return static_cast<float>(std::sqrt(sum / v.size()));
}

// Share of samples within 0.1% of the loudest one; a hard clip at 200% puts a quarter there.
float FlatShare(const std::vector<float>& v) {
  const float top = Peak(v);
  int near = 0;
  for (float x : v) near += std::fabs(x) >= 0.999f * top;
  return static_cast<float>(near) / v.size();
}

TEST(RenderLimiter_BoostOfOneLeavesEverySampleUntouched) {
  auto in = Sine(30000.0f, kRate);
  auto out = in;
  RenderLimiter::Apply(out.data(), static_cast<int>(out.size()), 1.0f);
  CHECK(out == in);
}

TEST(RenderLimiter_QuietSpeechScalesExactlyBelowTheKnee) {
  auto in = Sine(8000.0f, kRate);
  auto out = in;
  RenderLimiter::Apply(out.data(), static_cast<int>(out.size()), 2.0f);
  float worst = 0;
  for (size_t i = 0; i < in.size(); ++i) {
    worst = std::max(worst, std::fabs(out[i] - 2.0f * in[i]));
  }
  CHECK(worst < 1e-3f);
}

TEST(RenderLimiter_NearFullScaleAtDoubleNeverClipsAndIsNotFlatTopped) {
  const float boosts[] = {1.5f, 2.0f};
  for (float boost : boosts) {
    auto out = Sine(0.7f * kFullScale, kRate);
    const float crest_in = Peak(out) / Rms(out);
    RenderLimiter::Apply(out.data(), static_cast<int>(out.size()), boost);
    const float crest = Peak(out) / Rms(out);
    std::printf("  boost %.1fx: peak %.0f of %.0f, crest %.2f (sine %.2f), flat share %.3f\n",
                boost, Peak(out), kFullScale, crest, crest_in, FlatShare(out));
    CHECK(Peak(out) <= kFullScale);
    CHECK(FlatShare(out) < 0.12f);
    CHECK(crest > 1.2f);
  }
}

TEST(RenderLimiter_OutputRisesWithInputSoNothingFoldsBack) {
  float last = 0;
  for (float x = 0; x <= 40000.0f; x += 50.0f) {
    float s = x;
    RenderLimiter::Apply(&s, 1, 2.0f);
    CHECK(s >= last);
    last = s;
  }
}

TEST(RenderLimiter_ProcessShapesEveryChannelOfTheMix) {
  const int frames = 160;
  auto left = Sine(0.9f * kFullScale, frames);
  std::vector<float> stereo = left;
  stereo.insert(stereo.end(), left.begin(), left.end());
  RenderLimiter::Instance()->Initialize(kRate, 2);
  RenderLimiter::Instance()->SetBoost(3.0f);
  RenderLimiter::Instance()->Process(1, frames, frames, stereo.data());
  CHECK(*std::max_element(stereo.begin(), stereo.end()) <= kFullScale);
  CHECK(std::equal(stereo.begin(), stereo.begin() + frames, stereo.begin() + frames));
  RenderLimiter::Instance()->SetBoost(0.2f);
  CHECK(RenderLimiter::Instance()->Boost() == 1.0f);
  RenderLimiter::Instance()->Initialize(kRate, 1);
}

TEST(RenderGains_NobodyAboveFullLeavesLibwebrtcAtTheRequestedVolume) {
  RenderGains gains;
  gains.Set("a", 0.5);
  const std::vector<std::string> live = {"a", "b"};
  CHECK(gains.Boost(live) == 1.0);
  auto v = gains.Volumes(live);
  CHECK(v[0].volume == 0.5);
  CHECK(v[1].volume == 1.0);
}

TEST(RenderGains_ALoudPersonScalesTheOthersDownByTheSameFactor) {
  RenderGains gains;
  gains.Set("a", 2.0);
  gains.Set("b", 0.5);
  const std::vector<std::string> live = {"a", "b", "c"};
  CHECK(gains.Boost(live) == 2.0);
  auto v = gains.Volumes(live);
  CHECK(v[0].volume == 1.0);
  CHECK(v[1].volume == 0.25);
  CHECK(v[2].volume == 0.5);
}

TEST(RenderGains_APersonWhoLeftNoLongerRaisesTheBoost) {
  RenderGains gains;
  gains.Set("a", 2.0);
  CHECK(gains.Boost({"b"}) == 1.0);
  gains.Forget("a");
  CHECK(gains.Boost({"a"}) == 1.0);
}

}  // namespace
