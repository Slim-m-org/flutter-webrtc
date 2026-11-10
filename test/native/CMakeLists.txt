cmake_minimum_required(VERSION 3.10)
project(flutter_webrtc_native_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${ROOT}/third_party/rnnoise/CMakeLists.txt")
include("${ROOT}/third_party/dfn/CMakeLists.txt")

add_executable(native_tests
  strong_noise_suppression_test.cc
  strongest_test.cc
  input_gate_vad_test.cc
  input_gate_meter_test.cc
  input_gate_lookahead_test.cc
  capture_chain_burst_test.cc
  voice_leveler_test.cc
  frame_fit_test.cc
  check_registry.cc
  main.cc
  "${ROOT}/common/cpp/src/flutter_noise_suppressor.cc"
  "${ROOT}/common/cpp/src/flutter_deep_filter.cc"
  "${ROOT}/common/cpp/src/flutter_onset_cover.cc"
  "${ROOT}/common/cpp/src/flutter_rate_converter.cc"
  "${ROOT}/common/cpp/src/flutter_input_gate.cc"
  "${ROOT}/common/cpp/src/flutter_voice_dynamics.cc"
  "${ROOT}/common/cpp/src/flutter_capture_chain.cc"
  "${ROOT}/common/cpp/src/flutter_frame_fit.cc"
)
target_include_directories(native_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stub"
  "${ROOT}/common/cpp/include"
  "${ROOT}/linux/flutter/include"
)
target_link_libraries(native_tests PRIVATE slimm_rnnoise)
if(TARGET slimm_dfn)
  target_link_libraries(native_tests PRIVATE slimm_dfn)
endif()

# Apart from native_tests, which replaces operator new as -fsanitize=thread does.
find_package(Threads REQUIRED)
add_executable(thread_tests
  locked_map_test.cc
  serial_executor_test.cc
  request_coalescer_test.cc
  loopback_session_test.cc
  check_registry.cc
  main.cc
)
target_include_directories(thread_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stub"
  "${ROOT}/common/cpp/include"
)
target_link_libraries(thread_tests PRIVATE Threads::Threads)

# Runs a raw float file through the suppressor the way the capture chain does,
# for measuring Strongest offline: strongest_tool in.f32 out.f32 [strongest]
add_executable(strongest_tool strongest_tool.cc
  "${ROOT}/common/cpp/src/flutter_noise_suppressor.cc"
  "${ROOT}/common/cpp/src/flutter_deep_filter.cc"
  "${ROOT}/common/cpp/src/flutter_onset_cover.cc"
  "${ROOT}/common/cpp/src/flutter_rate_converter.cc"
)
target_include_directories(strongest_tool PRIVATE "${ROOT}/common/cpp/include")
target_link_libraries(strongest_tool PRIVATE slimm_rnnoise)
if(TARGET slimm_dfn)
  target_link_libraries(strongest_tool PRIVATE slimm_dfn)
endif()

# The Linux portal session guard on a private session bus; glib makes it its own binary.
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(GIO IMPORTED_TARGET gio-2.0)
endif()
if(TARGET PkgConfig::GIO)
  add_executable(portal_guard_tests
    portal_session_guard_test.cc
    check_registry.cc
    "${ROOT}/linux/portal_session_guard.cc"
  )
  target_compile_definitions(portal_guard_tests PRIVATE FLUTTER_WEBRTC_PORTAL_GUARD)
  target_include_directories(portal_guard_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${ROOT}/common/cpp/include"
  )
  target_link_libraries(portal_guard_tests PRIVATE PkgConfig::GIO Threads::Threads)
endif()
