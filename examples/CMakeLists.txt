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

# twitter example: provide path to twitter.json
set(TWITTER_JSON
    "${CMAKE_CURRENT_SOURCE_DIR}/../bench/json_examples/twitter.json")
if(TARGET twitter)
  target_compile_definitions(twitter PRIVATE
      TWITTER_JSON_PATH="${TWITTER_JSON}")

  if(CXX_HOSQ_ENABLE_ASSEMBLY AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(TWITTER_ASM_DIR "${CMAKE_CURRENT_BINARY_DIR}/asm")
    set(TWITTER_ASM_FILE "${TWITTER_ASM_DIR}/twitter.s")
    file(MAKE_DIRECTORY "${TWITTER_ASM_DIR}")
    add_custom_command(
      OUTPUT "${TWITTER_ASM_FILE}"
      COMMAND "${CMAKE_CXX_COMPILER}"
              -S
              -O2
              -std=gnu++20
              -I${CMAKE_CURRENT_SOURCE_DIR}/../include
              -DTWITTER_JSON_PATH="${TWITTER_JSON}"
              ${CMAKE_CURRENT_SOURCE_DIR}/twitter.cc
              -o ${TWITTER_ASM_FILE}
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/twitter.cc" ${HOSQ_PUBLIC_HEADERS} hosq
      COMMENT "Generating assembly for twitter example"
      VERBATIM
    )
    add_custom_target(twitter_asm DEPENDS "${TWITTER_ASM_FILE}")
  endif()

  # PGO instrumentation: build a separate instrumented variant when requested.
  option(HOSQ_EXAMPLES_PGO_INSTRUMENT
      "Build twitter_pgo_instr with -fprofile-instr-generate" OFF)
  if(HOSQ_EXAMPLES_PGO_INSTRUMENT AND
     CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_hosq_example("${CMAKE_CURRENT_SOURCE_DIR}/twitter.cc"
                     "_pgo_instr" cxx_std_20)
    target_compile_definitions(twitter_pgo_instr PRIVATE
        TWITTER_JSON_PATH="${TWITTER_JSON}")
    target_compile_options(twitter_pgo_instr PRIVATE
        -fprofile-instr-generate -O2)
    target_link_options(twitter_pgo_instr PRIVATE
        -fprofile-instr-generate)
  endif()
endif()
