#include <android/log.h>
#include <jni.h>

#include <atomic>

#include "flutter_capture_chain_api.h"
#include "flutter_chain_glue.h"

namespace {

// One line per capture start or settings change, so logcat shows what the chain runs.
std::atomic<bool> g_logged_first_frame{false};
std::atomic<int> g_logged_stages{-1};

}  // namespace

#define CHAIN_JNI(name) \
  Java_com_cloudwebrtc_webrtc_audio_CaptureChainProcessing_##name

extern "C" {

JNIEXPORT void JNICALL CHAIN_JNI(nativeInitialize)(JNIEnv*, jclass,
                                                   jint sample_rate_hz,
                                                   jint num_channels) {
  slimm_chain_initialize(sample_rate_hz, num_channels);
  g_logged_first_frame.store(false);
  __android_log_print(ANDROID_LOG_INFO, "CaptureChain",
                      "initialized at %d Hz, %d channel(s)", sample_rate_hz,
                      num_channels);
}

// The buffer wraps libwebrtc's own first-channel samples, so the chain edits
// what is sent without a copy.
JNIEXPORT void JNICALL CHAIN_JNI(nativeProcess)(JNIEnv* env, jclass,
                                                jobject buffer,
                                                jint num_frames) {
  auto* samples = static_cast<float*>(env->GetDirectBufferAddress(buffer));
  const jlong capacity = env->GetDirectBufferCapacity(buffer);
  if (!flutter_webrtc_plugin::FrameFitsBuffer(samples, capacity, num_frames)) {
    return;
  }
  slimm_chain_process(samples, num_frames);
  if (!g_logged_first_frame.exchange(true)) {
    __android_log_print(ANDROID_LOG_INFO, "CaptureChain",
                        "processing %d-sample frames, strong=%d", num_frames,
                        slimm_chain_strong_active());
  }
}

JNIEXPORT void JNICALL CHAIN_JNI(nativeConfigure)(
    JNIEnv*, jclass, jboolean gate_enabled, jfloat threshold_db,
    jboolean dynamics, jboolean level_after_suppression, jboolean strong) {
  slimm_chain_configure(gate_enabled, threshold_db, dynamics,
                        level_after_suppression, strong);
  // Automatic sensitivity moves the threshold often, so only the stages are logged.
  const int stages = flutter_webrtc_plugin::ChainStageMask(
      gate_enabled, strong, dynamics, level_after_suppression);
  if (g_logged_stages.exchange(stages) == stages) return;
  g_logged_first_frame.store(false);
  __android_log_print(ANDROID_LOG_INFO, "CaptureChain",
                      "configured gate=%d, strong=%d, dynamics=%d, leveler=%d",
                      gate_enabled, strong, dynamics, level_after_suppression);
}

// Level, voice, dynamics, leveler gain and Strong, in that order.
JNIEXPORT jfloatArray JNICALL CHAIN_JNI(nativeRead)(JNIEnv* env, jclass) {
  const jfloat values[] = {
      slimm_chain_take_peak_db(),
      static_cast<jfloat>(slimm_chain_voice()),
      static_cast<jfloat>(slimm_chain_dynamics()),
      slimm_chain_leveler_gain_db(),
      static_cast<jfloat>(slimm_chain_strong_active()),
  };
  jfloatArray out = env->NewFloatArray(5);
  if (out != nullptr) env->SetFloatArrayRegion(out, 0, 5, values);
  return out;
}

}  // extern "C"
