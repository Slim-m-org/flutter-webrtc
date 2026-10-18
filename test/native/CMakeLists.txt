cmake_minimum_required(VERSION 3.10)
project(flutter_webrtc_native_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${ROOT}/third_party/rnnoise/CMakeLists.txt")

add_executable(native_tests
  strong_noise_suppression_test.cc
  input_gate_vad_test.cc
  input_gate_meter_test.cc
  main.cc
  "${ROOT}/common/cpp/src/flutter_noise_suppressor.cc"
  "${ROOT}/common/cpp/src/flutter_rate_converter.cc"
  "${ROOT}/common/cpp/src/flutter_input_gate.cc"
)
target_include_directories(native_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stub"
  "${ROOT}/common/cpp/include"
)
target_link_libraries(native_tests PRIVATE slimm_rnnoise)
