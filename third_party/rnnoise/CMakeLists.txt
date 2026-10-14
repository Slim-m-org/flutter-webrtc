# rnnoise 0.2 (BSD-3-Clause, see COPYING), the generic C build without SIMD dispatch.
enable_language(C)

set(RNNOISE_DIR "${CMAKE_CURRENT_LIST_DIR}")
add_library(slimm_rnnoise STATIC
  "${RNNOISE_DIR}/src/denoise.c"
  "${RNNOISE_DIR}/src/rnn.c"
  "${RNNOISE_DIR}/src/pitch.c"
  "${RNNOISE_DIR}/src/kiss_fft.c"
  "${RNNOISE_DIR}/src/celt_lpc.c"
  "${RNNOISE_DIR}/src/nnet.c"
  "${RNNOISE_DIR}/src/nnet_default.c"
  "${RNNOISE_DIR}/src/parse_lpcnet_weights.c"
  "${RNNOISE_DIR}/src/rnnoise_data.c"
  "${RNNOISE_DIR}/src/rnnoise_tables.c"
)
target_include_directories(slimm_rnnoise
  PUBLIC "${RNNOISE_DIR}/include"
  PRIVATE "${RNNOISE_DIR}/src"
)
# C11 for `restrict` and `inline`, which MSVC's default C mode rejects.
set_target_properties(slimm_rnnoise PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
)
if(MSVC)
  target_compile_options(slimm_rnnoise PRIVATE /W0 /O2)
  target_compile_definitions(slimm_rnnoise PRIVATE _USE_MATH_DEFINES)
else()
  target_compile_options(slimm_rnnoise PRIVATE -w -O2)
  find_library(RNNOISE_LIBM m)
  if(RNNOISE_LIBM)
    target_link_libraries(slimm_rnnoise PRIVATE ${RNNOISE_LIBM})
  endif()
endif()
