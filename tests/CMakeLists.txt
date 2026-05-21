find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  set(INSTALL_GTEST OFF)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    URL      https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "*.cc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}")

function(add_hosq_tests suffix cxx_std)
  set(target_name hosq_tests${suffix})
  add_executable(${target_name} ${TEST_SOURCES})
  set_target_properties(${target_name} PROPERTIES CXX_CLANG_TIDY "")
  target_link_libraries(${target_name} PRIVATE hosq GTest::gtest GTest::gtest_main)
  string(REGEX REPLACE "cxx_std_" "" cxx_std_num ${cxx_std})
  set_target_properties(${target_name} PROPERTIES
    CXX_STANDARD ${cxx_std_num}
    CXX_STANDARD_REQUIRED ON
  )
  target_compile_options(${target_name} PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4;/WX,-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>
  )
  string(REGEX REPLACE "^_" "" test_prefix ${suffix})
  gtest_discover_tests(${target_name}
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 60
    TEST_PREFIX "${test_prefix}/"
  )
endfunction()

add_hosq_tests("_cxx20" cxx_std_20)
if(NOT MSVC)
  add_hosq_tests("_cxx26" cxx_std_26)
endif()
