# The capture chain from common/cpp (Strong, the input gate and the dynamics),
# run in libwebrtc's capture post-processing hook; see CaptureChainProcessing.
cmake_minimum_required(VERSION 3.18)
project(slimm_capture_chain LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/third_party/rnnoise/CMakeLists.txt")

add_library(slimm_capture_chain SHARED
  capture_chain_jni.cc
  "${ROOT}/common/cpp/src/flutter_capture_chain_api.cc"
  "${ROOT}/common/cpp/src/flutter_capture_chain.cc"
  "${ROOT}/common/cpp/src/flutter_input_gate.cc"
  "${ROOT}/common/cpp/src/flutter_noise_suppressor.cc"
  "${ROOT}/common/cpp/src/flutter_deep_filter.cc"
  "${ROOT}/common/cpp/src/flutter_onset_cover.cc"
  "${ROOT}/common/cpp/src/flutter_rate_converter.cc"
  "${ROOT}/common/cpp/src/flutter_voice_dynamics.cc"
  "${ROOT}/common/cpp/src/flutter_mic_sample_tap.cc"
)
target_include_directories(slimm_capture_chain PRIVATE
  "${ROOT}/common/cpp/portable"
  "${ROOT}/common/cpp/include"
)
target_compile_options(slimm_capture_chain PRIVATE -O3 -fvisibility=hidden)
target_link_libraries(slimm_capture_chain PRIVATE slimm_rnnoise log)
# 16 KB pages, which Android 15 devices may use and Play requires.
target_link_options(slimm_capture_chain PRIVATE "-Wl,-z,max-page-size=16384")
