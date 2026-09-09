cmake_minimum_required(VERSION 3.20)
project(hosq_comparison LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(benchmark CONFIG REQUIRED)

set(YYJSON_SOURCE "" CACHE FILEPATH "Path to yyjson.c")
if(NOT EXISTS "${YYJSON_SOURCE}")
  message(FATAL_ERROR "Set YYJSON_SOURCE to yyjson.c")
endif()
get_filename_component(YYJSON_INCLUDE "${YYJSON_SOURCE}" DIRECTORY)
add_library(comparison_yyjson STATIC "${YYJSON_SOURCE}")
target_include_directories(comparison_yyjson PUBLIC "${YYJSON_INCLUDE}")

set(GLAZE_SOURCE_DIR "" CACHE PATH "Path to Glaze 8.3.0")
if(NOT EXISTS "${GLAZE_SOURCE_DIR}/include/glaze/glaze.hpp")
  message(FATAL_ERROR "Set GLAZE_SOURCE_DIR to Glaze 8.3.0")
endif()

option(CXX_HOSQ_USE_FAST_FLOAT "Use bundled fast_float" ON)

function(add_comparison_case name)
  add_executable("hosq_bench_${name}" "${name}.cc")
  target_include_directories("hosq_bench_${name}"
    SYSTEM PRIVATE "${GLAZE_SOURCE_DIR}/include")
  target_include_directories("hosq_bench_${name}" PRIVATE ../../include)
  target_link_libraries("hosq_bench_${name}"
    PRIVATE comparison_yyjson benchmark::benchmark)
  target_compile_definitions("hosq_bench_${name}" PRIVATE
    HOSQ_USE_FAST_FLOAT=$<BOOL:${CXX_HOSQ_USE_FAST_FLOAT}>)
endfunction()

# One source file and one executable per benchmark case.
add_comparison_case(id_only)
add_comparison_case(partial_fields)
add_comparison_case(all_fields)
