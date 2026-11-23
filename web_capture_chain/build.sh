#!/usr/bin/env bash
# Builds capture_chain.wasm, the desktop capture chain (Strong, the input gate
# and the dynamics) for the web client's AudioWorklet, from common/cpp and
# third_party/rnnoise. The output is committed so an app build needs no
# Emscripten; rerun this after changing any source it lists, and commit both.
# Needs Docker; the image is pinned by digest so a rebuild is byte-identical.
set -euo pipefail

cd "$(dirname "$0")/.."
IMAGE=emscripten/emsdk:4.0.10@sha256:90b757eb11fa9a0e3ce4d2d9f76d932a56018e4accc37b5a28b2783751e60eb7

SOURCES=(
  common/cpp/src/flutter_capture_chain_api.cc
  common/cpp/src/flutter_capture_chain.cc
  common/cpp/src/flutter_input_gate.cc
  common/cpp/src/flutter_noise_suppressor.cc
  common/cpp/src/flutter_deep_filter.cc
  common/cpp/src/flutter_onset_cover.cc
  common/cpp/src/flutter_rate_converter.cc
  common/cpp/src/flutter_voice_dynamics.cc
  common/cpp/src/flutter_mic_sample_tap.cc
  third_party/rnnoise/src/denoise.c
  third_party/rnnoise/src/rnn.c
  third_party/rnnoise/src/pitch.c
  third_party/rnnoise/src/kiss_fft.c
  third_party/rnnoise/src/celt_lpc.c
  third_party/rnnoise/src/nnet.c
  third_party/rnnoise/src/nnet_default.c
  third_party/rnnoise/src/parse_lpcnet_weights.c
  third_party/rnnoise/src/rnnoise_data.c
  third_party/rnnoise/src/rnnoise_tables.c
)
EXPORTS=_slimm_chain_initialize,_slimm_chain_process,_slimm_chain_configure,_slimm_chain_take_peak_db,_slimm_chain_voice,_slimm_chain_dynamics,_slimm_chain_leveler_gain_db,_slimm_chain_strong_active,_slimm_chain_frame_buffer

# -mavx lets rnnoise take its x86 vector path, which Emscripten lowers to wasm SIMD: about 1.2% of a core instead of 3%.
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD:/src" -w /src -e HOME=/tmp "$IMAGE" \
  emcc -O3 -msimd128 -mavx -fno-exceptions -fno-rtti -w \
  -ffile-prefix-map=/src=. \
  -Icommon/cpp/portable -Icommon/cpp/include \
  -Ithird_party/rnnoise/include -Ithird_party/rnnoise/src \
  -sSTANDALONE_WASM --no-entry \
  -sINITIAL_MEMORY=16MB -sALLOW_MEMORY_GROWTH=0 -sSTACK_SIZE=262144 \
  -sEXPORTED_FUNCTIONS="$EXPORTS" \
  "${SOURCES[@]}" -o web_capture_chain/capture_chain.wasm

chmod -x web_capture_chain/capture_chain.wasm
sha256sum web_capture_chain/capture_chain.wasm
