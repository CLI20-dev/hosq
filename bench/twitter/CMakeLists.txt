find_package(PkgConfig REQUIRED)
pkg_check_modules(YYJSON REQUIRED yyjson)

add_executable(bench_twitter bench_twitter.cc)
target_link_libraries(bench_twitter
    PRIVATE
        benchmark::benchmark
        ser20::ser20
        ${YYJSON_LIBRARIES}
)
target_include_directories(bench_twitter PRIVATE ${YYJSON_INCLUDE_DIRS})
target_link_directories(bench_twitter PRIVATE ${YYJSON_LIBRARY_DIRS})
target_compile_options(bench_twitter PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
    ${YYJSON_CFLAGS_OTHER}
)

set(TWITTER_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../json_examples/twitter.json")
target_compile_definitions(bench_twitter PRIVATE
    TWITTER_JSON_PATH="${TWITTER_JSON}"
)
