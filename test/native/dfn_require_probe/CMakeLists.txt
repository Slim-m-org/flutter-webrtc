cmake_minimum_required(VERSION 3.10)
project(dfn_require_probe LANGUAGES C CXX)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${ROOT}/third_party/dfn/CMakeLists.txt")
