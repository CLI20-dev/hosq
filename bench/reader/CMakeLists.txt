add_executable(bench_reader bench_reader.cc)
target_link_libraries(bench_reader
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        ser20::ser20
        bench_alloc
)
target_compile_options(bench_reader PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
)

set(TWITTER_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../json_examples/twitter.json")
target_compile_definitions(bench_reader PRIVATE
    TWITTER_JSON_PATH="${TWITTER_JSON}"
)
