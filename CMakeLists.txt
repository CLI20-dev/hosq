cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" HOSQ_VERSION_RAW)
string(STRIP "${HOSQ_VERSION_RAW}" HOSQ_VERSION)

project(hosq VERSION ${HOSQ_VERSION} LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CXX_HOSQ_ENABLE_TEST "Enable building tests" OFF)
option(CXX_HOSQ_ENABLE_MODULE "Enable the HOSQ module" OFF)
option(CXX_HOSQ_ENABLE_ASSEMBLY "Enable assembly output targets" ON)

option(CXX_HOSQ_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
if(CXX_HOSQ_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE REQUIRED NAMES "clang-tidy")
  set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

option(CXX_HOSQ_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(CXX_HOSQ_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

file(GLOB_RECURSE HOSQ_PUBLIC_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hh"
)

add_library(hosq INTERFACE)
add_library(hosq::hosq ALIAS hosq)

target_sources(hosq
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES ${HOSQ_PUBLIC_HEADERS}
)

target_include_directories(hosq
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(hosq INTERFACE cxx_std_20)

set(HOSQ_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/hosq")
set(HOSQ_TARGETS_EXPORT_NAME hosqTargets)

install(
  TARGETS hosq
  EXPORT ${HOSQ_TARGETS_EXPORT_NAME}
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT ${HOSQ_TARGETS_EXPORT_NAME}
  FILE ${HOSQ_TARGETS_EXPORT_NAME}.cmake
  NAMESPACE hosq::
  DESTINATION ${HOSQ_INSTALL_CMAKEDIR}
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/hosqConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/hosqConfig.cmake
  INSTALL_DESTINATION ${HOSQ_INSTALL_CMAKEDIR}
  NO_SET_AND_CHECK_MACRO
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/hosqConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/hosqConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/hosqConfigVersion.cmake
    DESTINATION ${HOSQ_INSTALL_CMAKEDIR}
)


if(MSVC)
  message(STATUS "single_header target is not supported with MSVC – skipping")
else()
  file(GLOB HOSQ_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/cli/*.hh"
  )
  set(HOSQ_SINGLE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/single_header/hosq.hh")
  add_custom_command(
    OUTPUT "${HOSQ_SINGLE_HEADER}"
    COMMAND sh
      "${CMAKE_CURRENT_SOURCE_DIR}/script/make_single_header.sh"
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${HOSQ_SINGLE_HEADER}"
      "${CMAKE_CXX_COMPILER}"
      DEPENDS ${HOSQ_HEADERS}
    COMMENT "Generating single_header/hosq.hh"
  )
  add_custom_target(hosq_single_header DEPENDS "${HOSQ_SINGLE_HEADER}")
endif()

add_subdirectory(examples)

if(CXX_HOSQ_ENABLE_ASSEMBLY AND TARGET twitter_asm)
  add_custom_target(hosq_asm DEPENDS twitter_asm)
endif()

if(CXX_HOSQ_ENABLE_TEST)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CXX_HOSQ_ENABLE_MODULE)
  add_subdirectory(modules)
endif()
