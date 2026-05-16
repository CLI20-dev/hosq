cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SER20_VERSION_RAW)
string(STRIP "${SER20_VERSION_RAW}" SER20_VERSION)

project(ser20 VERSION ${SER20_VERSION} LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CXX_SER20_ENABLE_TEST "Enable building tests" OFF)
option(CXX_SER20_ENABLE_MODULE "Enable the SER20 module" OFF)

option(CXX_SER20_ENABLE_CLANG_TIDY "Enable clang-tidy checks" OFF)
if(CXX_SER20_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE REQUIRED NAMES "clang-tidy")
  set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

option(CXX_SER20_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(CXX_SER20_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

file(GLOB_RECURSE SER20_PUBLIC_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hh"
)

add_library(ser20 INTERFACE)
add_library(ser20::ser20 ALIAS ser20)

target_sources(ser20
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
        FILES ${SER20_PUBLIC_HEADERS}
)

target_include_directories(ser20
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(ser20 INTERFACE cxx_std_20)

set(SER20_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/ser20")
set(SER20_TARGETS_EXPORT_NAME ser20Targets)

install(
  TARGETS ser20
  EXPORT ${SER20_TARGETS_EXPORT_NAME}
  FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  EXPORT ${SER20_TARGETS_EXPORT_NAME}
  FILE ${SER20_TARGETS_EXPORT_NAME}.cmake
  NAMESPACE ser20::
  DESTINATION ${SER20_INSTALL_CMAKEDIR}
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ser20Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ser20Config.cmake
  INSTALL_DESTINATION ${SER20_INSTALL_CMAKEDIR}
  NO_SET_AND_CHECK_MACRO
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ser20ConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ser20Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ser20ConfigVersion.cmake
    DESTINATION ${SER20_INSTALL_CMAKEDIR}
)


if(MSVC)
  message(STATUS "single_header target is not supported with MSVC – skipping")
else()
  file(GLOB SER20_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/cli/*.hh"
  )
  set(SER20_SINGLE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/single_header/ser20.hh")
  add_custom_command(
    OUTPUT "${SER20_SINGLE_HEADER}"
    COMMAND sh
      "${CMAKE_CURRENT_SOURCE_DIR}/script/make_single_header.sh"
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${SER20_SINGLE_HEADER}"
      "${CMAKE_CXX_COMPILER}"
      DEPENDS ${SER20_HEADERS}
    COMMENT "Generating single_header/ser20.hh"
  )
  add_custom_target(ser20_single_header DEPENDS "${SER20_SINGLE_HEADER}")
endif()

add_subdirectory(examples)

if(CXX_SER20_ENABLE_TEST)
  enable_testing()
  add_subdirectory(tests)
endif()

if(CXX_SER20_ENABLE_MODULE)
  add_subdirectory(modules)
endif()
