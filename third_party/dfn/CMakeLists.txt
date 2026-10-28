# DeepFilterNet 3 (MIT or Apache-2.0) through tract, as a static library built
# with cargo. Defines the target slimm_dfn when it can be built and leaves the
# plugin to build without it otherwise, where "Strongest" falls back to Strong.
set(SLIMM_DFN_DIR "${CMAKE_CURRENT_LIST_DIR}")

find_program(SLIMM_CARGO cargo HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin")

set(_slimm_dfn_skip "")
if(NOT SLIMM_CARGO)
  set(_slimm_dfn_skip "cargo was not found")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_GENERATOR_PLATFORM MATCHES "(ARM64|arm64)")
  set(_slimm_dfn_skip "tract has no tested Windows ARM64 build here")
endif()

if(_slimm_dfn_skip)
  message(WARNING "flutter_webrtc: DeepFilterNet (Strongest noise suppression) not built: ${_slimm_dfn_skip}.")
else()
  set(_slimm_dfn_target_dir "${CMAKE_CURRENT_BINARY_DIR}/slimm_dfn_target")
  if(MSVC)
    set(_slimm_dfn_lib "${_slimm_dfn_target_dir}/release/slimm_dfn.lib")
  else()
    set(_slimm_dfn_lib "${_slimm_dfn_target_dir}/release/libslimm_dfn.a")
  endif()
  add_custom_command(
    OUTPUT "${_slimm_dfn_lib}"
    COMMAND ${CMAKE_COMMAND} -E env "CARGO_TARGET_DIR=${_slimm_dfn_target_dir}"
            "${SLIMM_CARGO}" build --release --locked
            --manifest-path "${SLIMM_DFN_DIR}/Cargo.toml"
    DEPENDS "${SLIMM_DFN_DIR}/Cargo.toml" "${SLIMM_DFN_DIR}/Cargo.lock"
            "${SLIMM_DFN_DIR}/src/lib.rs"
    COMMENT "Building DeepFilterNet with cargo"
    VERBATIM
  )
  add_custom_target(slimm_dfn_build DEPENDS "${_slimm_dfn_lib}")
  add_library(slimm_dfn STATIC IMPORTED GLOBAL)
  set_target_properties(slimm_dfn PROPERTIES IMPORTED_LOCATION "${_slimm_dfn_lib}")
  add_dependencies(slimm_dfn slimm_dfn_build)
  set_property(TARGET slimm_dfn PROPERTY INTERFACE_COMPILE_DEFINITIONS SLIMM_HAS_DFN=1)
  # What rustc lists for a staticlib that uses std, threads and files.
  if(WIN32)
    set_property(TARGET slimm_dfn PROPERTY INTERFACE_LINK_LIBRARIES
      kernel32.lib ntdll.lib userenv.lib ws2_32.lib dbghelp.lib bcrypt.lib advapi32.lib)
  else()
    set_property(TARGET slimm_dfn PROPERTY INTERFACE_LINK_LIBRARIES pthread dl m)
  endif()
endif()
