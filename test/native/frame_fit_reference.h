#ifndef TEST_NATIVE_FRAME_FIT_REFERENCE_H_
#define TEST_NATIVE_FRAME_FIT_REFERENCE_H_

#include "flutter_frame_fit.h"

namespace frame_fit_reference {

// The scaler FrameFitter shipped with first (fork e70a980), kept as the
// yardstick the tests hold today's output to and the benchmark times against.
void Scale(const flutter_webrtc_plugin::I420View& in,
           flutter_webrtc_plugin::ScaledI420* out);

}  // namespace frame_fit_reference

#endif  // TEST_NATIVE_FRAME_FIT_REFERENCE_H_
