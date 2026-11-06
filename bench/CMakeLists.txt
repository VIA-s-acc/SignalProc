add_executable(signals_bench bench_core.cpp bench_tui.cpp bench_pipeline.cpp)
target_link_libraries(signals_bench PRIVATE signals_core signals_io signals_tui signals_pipeline nanobench::nanobench)
target_compile_definitions(signals_bench PRIVATE SIGNALS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
signals_configure_target(signals_bench)
