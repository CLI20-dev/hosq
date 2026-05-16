add_executable(bench_scanner bench_scanner.cc)
target_link_libraries(bench_scanner
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        ser20::ser20
)
target_compile_options(bench_scanner PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
)
