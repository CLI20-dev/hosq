find_package(simdjson REQUIRED)

add_executable(bench_twitter twitter_bench.cc)
target_link_libraries(bench_twitter
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        ser20::ser20
        simdjson::simdjson
)
target_compile_options(bench_twitter PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
)

set(TWITTER_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../json_examples/twitter.json")
target_compile_definitions(bench_twitter PRIVATE
    TWITTER_JSON_PATH="${TWITTER_JSON}"
)
