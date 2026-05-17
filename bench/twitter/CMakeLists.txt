find_package(simdjson REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(yyjson REQUIRED yyjson)
find_package(RapidJSON REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(glaze REQUIRED)

add_executable(bench_twitter twitter_bench.cc)
target_link_libraries(bench_twitter
    PRIVATE
        benchmark::benchmark
        benchmark::benchmark_main
        ser20::ser20
        simdjson::simdjson
        ${yyjson_LIBRARIES}
        nlohmann_json::nlohmann_json
        glaze::glaze
)
target_include_directories(bench_twitter PRIVATE
    ${yyjson_INCLUDE_DIRS}
    ${RAPIDJSON_INCLUDE_DIRS}
)
target_link_directories(bench_twitter PRIVATE ${yyjson_LIBRARY_DIRS})
target_compile_options(bench_twitter PRIVATE
    $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>
)

set(TWITTER_JSON "${CMAKE_CURRENT_SOURCE_DIR}/../json_examples/twitter.json")
target_compile_definitions(bench_twitter PRIVATE
    TWITTER_JSON_PATH="${TWITTER_JSON}"
)
