# Checks that third_party/CMakeLists.txt only uses a libwebrtc that hashes to the pin: cmake -DROOT=<repo> -DWORK=<scratch dir> -P <this file>
cmake_minimum_required(VERSION 3.21)

if(NOT ROOT OR NOT WORK)
  message(FATAL_ERROR "usage: cmake -DROOT=<repo> -DWORK=<scratch dir> -P libwebrtc_pin_test.cmake")
endif()
file(REMOVE_RECURSE "${WORK}")

set(_version "pin-test")
set(_assets linux-x64 linux-arm64 win-x64 win-arm64)
set(_failures 0)

# One zip with a marker file saying which build it is.
function(make_zip build out_var)
  set(_dir "${WORK}/zips/${build}")
  file(MAKE_DIRECTORY "${_dir}/libwebrtc")
  file(WRITE "${_dir}/libwebrtc/build.txt" "${build}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${_dir}.zip" --format=zip libwebrtc
                  WORKING_DIRECTORY "${_dir}" COMMAND_ERROR_IS_FATAL ANY)
  file(SHA256 "${_dir}.zip" _sha)
  set(${out_var} "${_dir}.zip" PARENT_SCOPE)
  set(${out_var}_SHA256 "${_sha}" PARENT_SCOPE)
endfunction()

# A sandbox third_party folder pinning `pinned_sha` (empty for no pin), with a release folder serving `served_zip`.
function(make_sandbox name pinned_sha served_zip)
  set(_sandbox "${WORK}/${name}")
  file(MAKE_DIRECTORY "${_sandbox}/third_party" "${_sandbox}/releases/${_version}")
  file(COPY "${ROOT}/third_party/CMakeLists.txt" DESTINATION "${_sandbox}/third_party")
  set(_ini "[libwebrtc]\nbinary_version = ${_version}\n")
  if(WIN32)
    set(_base "file:///${_sandbox}/releases")
  else()
    set(_base "file://${_sandbox}/releases")
  endif()
  string(APPEND _ini "download_url = ${_base}\n")
  foreach(_asset IN LISTS _assets)
    string(REPLACE "-" "_" _key "${_asset}")
    if(NOT pinned_sha STREQUAL "")
      string(APPEND _ini "sha256_${_key} = ${pinned_sha}\n")
    endif()
    if(served_zip)
      file(COPY_FILE "${served_zip}" "${_sandbox}/releases/${_version}/libwebrtc-${_asset}-release.zip")
    endif()
  endforeach()
  file(WRITE "${_sandbox}/third_party/libwebrtc_version.ini" "${_ini}")
  file(WRITE "${_sandbox}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.18)\n"
    "project(libwebrtc_pin_fixture NONE)\n"
    "set(CMAKE_SIZEOF_VOID_P 8)\n"
    "include(\"${_sandbox}/third_party/CMakeLists.txt\")\n")
endfunction()

function(configure name result_var output_var)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${WORK}/${name}" -B "${WORK}/${name}/build"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  set(${result_var} "${_result}" PARENT_SCOPE)
  set(${output_var} "${_out}${_err}" PARENT_SCOPE)
endfunction()

function(extracted_build name out_var)
  set(_marker "${WORK}/${name}/third_party/libwebrtc/build.txt")
  set(_found "")
  if(EXISTS "${_marker}")
    file(READ "${_marker}" _found)
  endif()
  set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

function(cached_zip name out_var)
  file(GLOB _zips "${WORK}/${name}/third_party/downloads/*.zip")
  set(${out_var} "${_zips}" PARENT_SCOPE)
endfunction()

macro(check_case title)
  if(${ARGN})
    message(STATUS "ok   ${title}")
  else()
    message(STATUS "FAIL ${title}")
    math(EXPR _failures "${_failures} + 1")
  endif()
endmacro()

make_zip(pinned _pinned)
make_zip(swapped _swapped)

make_sandbox(accepts_the_pinned_asset "${_pinned_SHA256}" "${_pinned}")
configure(accepts_the_pinned_asset result out)
extracted_build(accepts_the_pinned_asset build)
check_case("the pinned asset is downloaded and extracted" result EQUAL 0 AND build STREQUAL "pinned")

make_sandbox(refuses_a_swapped_asset "${_pinned_SHA256}" "${_swapped}")
configure(refuses_a_swapped_asset result out)
extracted_build(refuses_a_swapped_asset build)
cached_zip(refuses_a_swapped_asset zips)
check_case("an asset that is not the pinned one fails the configure" NOT result EQUAL 0 AND out MATCHES "sha256")
check_case("a swapped asset is neither extracted nor kept" NOT build AND NOT zips)

make_sandbox(replaces_a_tampered_cache "${_pinned_SHA256}" "${_pinned}")
file(MAKE_DIRECTORY "${WORK}/replaces_a_tampered_cache/third_party/downloads")
file(COPY_FILE "${_swapped}" "${WORK}/replaces_a_tampered_cache/third_party/downloads/libwebrtc-linux-x64-release.zip")
configure(replaces_a_tampered_cache result out)
extracted_build(replaces_a_tampered_cache build)
check_case("a cached zip that is not the pinned one is downloaded again" result EQUAL 0 AND build STREQUAL "pinned")

make_sandbox(keeps_a_matching_cache "${_pinned_SHA256}" "")
file(MAKE_DIRECTORY "${WORK}/keeps_a_matching_cache/third_party/downloads")
foreach(_asset IN LISTS _assets)
  file(COPY_FILE "${_pinned}" "${WORK}/keeps_a_matching_cache/third_party/downloads/libwebrtc-${_asset}-release.zip")
endforeach()
configure(keeps_a_matching_cache result out)
extracted_build(keeps_a_matching_cache build)
check_case("a cached zip that is the pinned one needs no download" result EQUAL 0 AND build STREQUAL "pinned")

make_sandbox(refuses_a_manifest_without_a_pin "" "${_pinned}")
configure(refuses_a_manifest_without_a_pin result out)
extracted_build(refuses_a_manifest_without_a_pin build)
check_case("a manifest without a sha256 fails the configure" NOT result EQUAL 0 AND out MATCHES "sha256" AND NOT build)

if(_failures GREATER 0)
  message(FATAL_ERROR "${_failures} case(s) failed")
endif()
