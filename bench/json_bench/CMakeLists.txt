find_package(PkgConfig REQUIRED)
pkg_check_modules(YYJSON REQUIRED yyjson)

add_executable(bench_json_bench json_bench.cc)
target_link_libraries(bench_json_bench
    PRIVATE
        benchmark::benchmark
        hosq::hosq
        ${YYJSON_LIBRARIES}
)
target_include_directories(bench_json_bench PRIVATE ${YYJSON_INCLUDE_DIRS})
target_link_directories(bench_json_bench PRIVATE ${YYJSON_LIBRARY_DIRS})
target_compile_options(bench_json_bench PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
    ${YYJSON_CFLAGS_OTHER}
)
