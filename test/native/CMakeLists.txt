cmake_minimum_required(VERSION 3.10)
project(flutter_webrtc_native_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Threads REQUIRED)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${ROOT}/third_party/rnnoise/CMakeLists.txt")
include("${ROOT}/third_party/dfn/CMakeLists.txt")

add_executable(native_tests
  strong_noise_suppression_test.cc
  strongest_test.cc
  strongest_model_test.cc
  input_gate_vad_test.cc
  input_gate_meter_test.cc
  input_gate_lookahead_test.cc
  capture_chain_burst_test.cc
  voice_leveler_test.cc
  frame_fit_test.cc
  render_limiter_test.cc
  shared_state_test.cc
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
  "${ROOT}/common/cpp/src/flutter_render_limiter.cc"
  "${ROOT}/common/cpp/src/flutter_render_gains.cc"
)
target_include_directories(native_tests PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/stub"
  "${ROOT}/common/cpp/include"
  "${ROOT}/linux/flutter/include"
)
target_link_libraries(native_tests PRIVATE slimm_rnnoise Threads::Threads)
if(TARGET slimm_dfn)
  target_link_libraries(native_tests PRIVATE slimm_dfn)
endif()

# GNU ld, gold and lld wrap a symbol, so these cases see which thread creates and frees the model.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET slimm_dfn)
  target_sources(native_tests PRIVATE strongest_model_threads_test.cc)
  target_link_options(native_tests PRIVATE
    -Wl,--wrap=slimm_dfn_create -Wl,--wrap=slimm_dfn_free)
endif()

# Apart from native_tests, which replaces operator new as -fsanitize=thread does.
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
if(UNIX)
  target_sources(thread_tests PRIVATE screen_session_env_test.cc)
endif()

# Runs a raw float file through the suppressor the way the capture chain does,
# for measuring Strongest offline: strongest_tool in.f32 out.f32 [strongest]
add_executable(strongest_tool strongest_tool.cc
  "${ROOT}/common/cpp/src/flutter_noise_suppressor.cc"
  "${ROOT}/common/cpp/src/flutter_deep_filter.cc"
  "${ROOT}/common/cpp/src/flutter_onset_cover.cc"
  "${ROOT}/common/cpp/src/flutter_rate_converter.cc"
)
target_include_directories(strongest_tool PRIVATE "${ROOT}/common/cpp/include")
target_link_libraries(strongest_tool PRIVATE slimm_rnnoise Threads::Threads)
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

# A binary whose only test skips, to check how the harness reports and counts one.
add_executable(harness_probe harness_probe.cc check_registry.cc)

# NATIVE_TESTS_STRICT=ON makes a skipped test fail, which CI uses so a missing model or bus is not a pass.
option(NATIVE_TESTS_STRICT "Fail on a skipped test" OFF)
set(_strict_flag "")
if(NATIVE_TESTS_STRICT)
  set(_strict_flag --strict)
endif()

enable_testing()
add_test(NAME native_tests COMMAND native_tests ${_strict_flag})
add_test(NAME thread_tests COMMAND thread_tests ${_strict_flag})
if(TARGET portal_guard_tests)
  add_test(NAME portal_guard_tests COMMAND portal_guard_tests ${_strict_flag})
endif()
add_test(NAME harness_reports_a_skip COMMAND harness_probe)
set_tests_properties(harness_reports_a_skip PROPERTIES
  PASS_REGULAR_EXPRESSION "skip +probe_skips.*0 failure\\(s\\), 1 skipped")
add_test(NAME harness_strict_fails_a_skip COMMAND harness_probe --strict)
set_tests_properties(harness_strict_fails_a_skip PROPERTIES WILL_FAIL TRUE)
add_test(NAME libwebrtc_download_is_pinned
  COMMAND "${CMAKE_COMMAND}" -DROOT=${ROOT} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/libwebrtc_pin
          -P "${CMAKE_CURRENT_LIST_DIR}/libwebrtc_pin_test.cmake")
