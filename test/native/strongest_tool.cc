#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "flutter_noise_suppressor.h"

using flutter_webrtc_plugin::NoiseSuppressor;

namespace {

float EnvFloat(const char* name, float fallback) {
  const char* value = std::getenv(name);
  return value == nullptr ? fallback : static_cast<float>(std::atof(value));
}

int EnvInt(const char* name, int fallback) {
  const char* value = std::getenv(name);
  return value == nullptr ? fallback : std::atoi(value);
}

}  // namespace

// usage: strongest_tool in.f32 out.f32 [strongest|strong]
// 48 kHz mono float in [-1, 1], 10 ms at a time. Writes the speech
// probability the gate would see to out.f32.prob. The tuning comes from the
// environment: DFN_ATTEN, DFN_MIN_DB, DFN_ERB_DB, DFN_DF_DB, COVER_CONFIRM,
// COVER_SPEECH, COVER_QUIET_FRAMES, COVER_FRAMES, COVER_RELEASE.
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const bool strongest = argc > 3 && std::string(argv[3]) == "strongest";
  FILE* in = std::fopen(argv[1], "rb");
  FILE* out = std::fopen(argv[2], "wb");
  const std::string prob_path = std::string(argv[2]) + ".prob";
  FILE* prob = std::fopen(prob_path.c_str(), "wb");
  if (in == nullptr || out == nullptr || prob == nullptr) return 2;

  NoiseSuppressor suppressor;
  NoiseSuppressor::StrongestTuning tuning;
  tuning.model.atten_lim_db = EnvFloat("DFN_ATTEN", tuning.model.atten_lim_db);
  tuning.model.min_db = EnvFloat("DFN_MIN_DB", tuning.model.min_db);
  tuning.model.erb_db = EnvFloat("DFN_ERB_DB", tuning.model.erb_db);
  tuning.model.df_db = EnvFloat("DFN_DF_DB", tuning.model.df_db);
  tuning.onset.confirm_frames = EnvInt("COVER_CONFIRM", tuning.onset.confirm_frames);
  tuning.onset.speech_probability =
      EnvFloat("COVER_SPEECH", tuning.onset.speech_probability);
  tuning.onset.quiet_frames = EnvInt("COVER_QUIET_FRAMES", tuning.onset.quiet_frames);
  tuning.speech_floor_db = EnvFloat("COVER_FLOOR", tuning.speech_floor_db);
  tuning.clip_margin_db = EnvFloat("COVER_MARGIN", tuning.clip_margin_db);
  tuning.onset.speaking_weight = EnvFloat("COVER_SPEAKING", tuning.onset.speaking_weight);
  tuning.onset.cover_frames = EnvInt("COVER_FRAMES", tuning.onset.cover_frames);
  tuning.onset.release_frames = EnvInt("COVER_RELEASE", tuning.onset.release_frames);
  suppressor.SetStrongestTuning(tuning);
  suppressor.Configure(true);
  if (strongest) suppressor.ConfigureStrongest(true);

  std::vector<float> frame(480);
  long frames = 0;
  long deep_frames = 0;
  double seconds = 0.0;
  while (std::fread(frame.data(), sizeof(float), 480, in) == 480) {
    for (float& v : frame) v *= 32768.0f;
    const auto start = std::chrono::steady_clock::now();
    const float p = suppressor.Process(frame.data(), 480);
    seconds += std::chrono::duration<double>(
                   std::chrono::steady_clock::now() - start)
                   .count();
    deep_frames += suppressor.StrongestActive() ? 1 : 0;
    ++frames;
    for (float& v : frame) v /= 32768.0f;
    std::fwrite(frame.data(), sizeof(float), 480, out);
    std::fwrite(&p, sizeof(float), 1, prob);
  }
  std::fprintf(stderr, "frames %ld deep %ld rtf %.5f\n", frames, deep_frames,
               seconds / (static_cast<double>(frames) * 0.01));
  std::fclose(in);
  std::fclose(out);
  std::fclose(prob);
  return 0;
}
