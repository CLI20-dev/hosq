add_executable(bench_scanner bench_scanner.cc)
target_link_libraries(bench_scanner
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        hosq::hosq
        bench_alloc
)
target_compile_options(bench_scanner PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
)

set(TWITTER_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../json_examples/twitter.json")
target_compile_definitions(bench_scanner PRIVATE
    TWITTER_JSON_PATH="${TWITTER_JSON}"
)
