add_executable(bench_matcher bench_matcher.cc)
target_link_libraries(bench_matcher
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        ser20::ser20
        bench_alloc
)
target_compile_options(bench_matcher PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
)
