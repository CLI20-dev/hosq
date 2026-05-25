cmake_minimum_required(VERSION 3.20)

project(hosq_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD "26")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(benchmark REQUIRED)

add_library(hosq INTERFACE)
add_library(hosq::hosq ALIAS hosq)
target_include_directories(hosq INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(hosq INTERFACE cxx_std_20)

# Allocation tracking: OBJECT library so operator new/delete overrides are
# always included in the final executable regardless of linker dead-stripping.
add_library(bench_alloc OBJECT
    allocation_counter.cc
    allocation_new.cc
)
target_include_directories(bench_alloc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(reader)
add_subdirectory(json_bench)
add_subdirectory(twitter)
