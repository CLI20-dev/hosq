function(add_hosq_example source_file suffix cxx_std)
  get_filename_component(example_name "${source_file}" NAME_WE)
  set(target_name "${example_name}${suffix}")

  add_executable("${target_name}" "${source_file}")
  set_target_properties("${target_name}" PROPERTIES CXX_CLANG_TIDY "")

  target_link_libraries("${target_name}" PRIVATE hosq)

  string(REGEX REPLACE "cxx_std_" "" cxx_std_num "${cxx_std}")
  set_target_properties("${target_name}" PROPERTIES
    CXX_STANDARD "${cxx_std_num}"
    CXX_STANDARD_REQUIRED ON
  )
  target_compile_options("${target_name}" PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4;/WX,-Wall;-Wextra;-Wpedantic;-Werror>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>
  )
endfunction()

file(GLOB EXAMPLE_SOURCES CONFIGURE_DEPENDS "*.cc")

if(NOT CXX_CLI20_ENABLE_MODULE)
  list(REMOVE_ITEM EXAMPLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/module.cc")
endif()

foreach(example_source ${EXAMPLE_SOURCES})
  get_filename_component(example_name "${example_source}" NAME_WE)
  add_hosq_example("${example_source}" "" cxx_std_20)
endforeach()
