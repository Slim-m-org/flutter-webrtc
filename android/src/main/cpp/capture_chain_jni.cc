#include <android/log.h>
#include <jni.h>

#include <algorithm>
#include <atomic>

#include "flutter_capture_chain_api.h"

namespace {

constexpr const char* kTag = "CaptureChain";

// The audio thread only writes these; the platform thread prints them.
std::atomic<bool> g_gate_enabled{false};
std::atomic<jlong> g_rejected_frames{0};
std::atomic<bool> g_reject_logged{false};
std::atomic<bool> g_init_pending{false};
std::atomic<int> g_init_rate{0};
std::atomic<int> g_init_channels{0};
std::atomic<int> g_first_frame_samples{0};
std::atomic<bool> g_first_frame_reported{false};
std::atomic<int> g_logged_stages{-1};

// A frame the chain cannot process leaves silent when the gate is on, never as the raw mic.
void FailClosed(float* samples, jlong floats_available, jint num_frames) {
  if (samples == nullptr || !g_gate_enabled.load(std::memory_order_relaxed)) return;
  const jlong count = std::min<jlong>(floats_available, num_frames);
  if (count > 0) std::fill_n(samples, count, 0.0f);
}

// Runs on the platform thread only, so no logd write ever lands on the audio callback.
void LogPending(bool stages_changed) {
  if (g_init_pending.exchange(false)) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "initialized at %d Hz, %d channel(s)",
                        g_init_rate.load(), g_init_channels.load());
  }
  if (stages_changed) g_first_frame_reported.store(false);
  const int frame_samples = g_first_frame_samples.load();
  if (frame_samples > 0 && !g_first_frame_reported.exchange(true)) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "processing %d-sample frames, strong=%d",
                        frame_samples, slimm_chain_strong_active());
  }
  const jlong rejected = g_rejected_frames.load();
  if (rejected > 0 && !g_reject_logged.exchange(true)) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "rejected %lld frame(s) the chain could not process; gate on=%d",
                        static_cast<long long>(rejected), g_gate_enabled.load() ? 1 : 0);
  }
}

}  // namespace

#define CHAIN_JNI(name) \
  Java_com_cloudwebrtc_webrtc_audio_CaptureChainProcessing_##name

extern "C" {

JNIEXPORT void JNICALL CHAIN_JNI(nativeInitialize)(JNIEnv*, jclass,
                                                   jint sample_rate_hz,
                                                   jint num_channels) {
  slimm_chain_initialize(sample_rate_hz, num_channels);
  g_init_rate.store(sample_rate_hz);
  g_init_channels.store(num_channels);
  g_first_frame_samples.store(0);
  g_reject_logged.store(false);
  g_init_pending.store(true);
}

// The buffer wraps libwebrtc's own first-channel samples, so the chain edits
// what is sent without a copy.
JNIEXPORT void JNICALL CHAIN_JNI(nativeProcess)(JNIEnv* env, jclass,
                                                jobject buffer,
                                                jint num_frames) {
  auto* samples = static_cast<float*>(env->GetDirectBufferAddress(buffer));
  const jlong capacity = env->GetDirectBufferCapacity(buffer);
  const jlong floats_available = capacity > 0 ? capacity / 4 : 0;
  if (samples == nullptr || num_frames <= 0 || floats_available < num_frames) {
    g_rejected_frames.fetch_add(1, std::memory_order_relaxed);
    FailClosed(samples, floats_available, num_frames);
    return;
  }
  if (slimm_chain_process(samples, num_frames) == 0) {
    g_rejected_frames.fetch_add(1, std::memory_order_relaxed);
    FailClosed(samples, floats_available, num_frames);
    return;
  }
  g_first_frame_samples.store(num_frames, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL CHAIN_JNI(nativeConfigure)(
    JNIEnv*, jclass, jboolean gate_enabled, jfloat threshold_db,
    jboolean dynamics, jboolean level_after_suppression, jboolean strong) {
  g_gate_enabled.store(gate_enabled != JNI_FALSE);
  slimm_chain_configure(gate_enabled, threshold_db, dynamics,
                        level_after_suppression, strong);
  // Automatic sensitivity moves the threshold often, so only the stages are logged.
  const int stages = (gate_enabled ? 1 : 0) | (strong ? 2 : 0) |
                     (dynamics ? 4 : 0) | (level_after_suppression ? 8 : 0);
  const bool changed = g_logged_stages.exchange(stages) != stages;
  if (changed) {
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "configured gate=%d, strong=%d, dynamics=%d, leveler=%d",
                        gate_enabled, strong, dynamics, level_after_suppression);
  }
  LogPending(changed);
}

// Level, voice, dynamics, leveler gain, Strong and rejected frames, in that order.
JNIEXPORT jfloatArray JNICALL CHAIN_JNI(nativeRead)(JNIEnv* env, jclass) {
  LogPending(false);
  const jfloat values[] = {
      slimm_chain_take_peak_db(),
      static_cast<jfloat>(slimm_chain_voice()),
      static_cast<jfloat>(slimm_chain_dynamics()),
      slimm_chain_leveler_gain_db(),
      static_cast<jfloat>(slimm_chain_strong_active()),
      static_cast<jfloat>(g_rejected_frames.load()),
  };
  jfloatArray out = env->NewFloatArray(6);
  if (out != nullptr) env->SetFloatArrayRegion(out, 0, 6, values);
  return out;
}

}  // extern "C"
