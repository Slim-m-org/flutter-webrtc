#ifndef FLUTTER_WEBRTC_PLUGIN_FLUTTER_CAPTURE_CHAIN_API_H_
#define FLUTTER_WEBRTC_PLUGIN_FLUTTER_CAPTURE_CHAIN_API_H_

// The capture chain (Strong, the input gate and the dynamics) for hosts that
// reach it without libwebrtc's C++ wrapper: Android over JNI, iOS from
// Objective-C++ and the web build compiled to WebAssembly. Desktop drives the
// same singletons from flutter_webrtc.cc. Strongest is desktop only, so it
// is not offered here. Plain C scalars, so a wasm export needs no glue.

#ifdef __cplusplus
extern "C" {
#endif

// The capture opened or changed format.
void slimm_chain_initialize(int sample_rate_hz, int num_channels);

// One 10 ms frame of mono samples on the int16 scale, in place, on the audio
// thread. Any rate the chain supports (8 to 48 kHz). Returns 0 when the frame
// is too long to process and was left untouched, 1 otherwise.
int slimm_chain_process(float* buffer, int num_frames);

// The same settings `setInputGate` takes on desktop.
void slimm_chain_configure(int gate_enabled, float threshold_db, int dynamics,
                           int level_after_suppression,
                           int strong_noise_suppression);

// `getInputGate`'s fields. The peak is the loudest frame since the last call
// and resets, so only one caller may poll it.
float slimm_chain_take_peak_db(void);
int slimm_chain_voice(void);
int slimm_chain_dynamics(void);
float slimm_chain_leveler_gain_db(void);
int slimm_chain_strong_active(void);

// Scratch memory for one frame, so the wasm host can copy samples in and out
// without an allocator of its own; up to 4800 samples (10 ms at 480 kHz).
float* slimm_chain_frame_buffer(void);

// The longest frame slimm_chain_process accepts.
int slimm_chain_max_frame_samples(void);

#ifdef __cplusplus
}
#endif

#endif  // FLUTTER_WEBRTC_PLUGIN_FLUTTER_CAPTURE_CHAIN_API_H_
