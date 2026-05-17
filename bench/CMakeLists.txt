cmake_minimum_required(VERSION 3.20)

project(ser20_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD "26")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(benchmark REQUIRED)

add_library(ser20 INTERFACE)
add_library(ser20::ser20 ALIAS ser20)
target_include_directories(ser20 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(ser20 INTERFACE cxx_std_20)

add_subdirectory(scanner)
add_subdirectory(matcher)
add_subdirectory(twitter)
