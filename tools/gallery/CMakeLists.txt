add_executable(signals-gallery main.cpp)
target_link_libraries(signals-gallery PRIVATE signals_tui signals_learn)
target_compile_definitions(signals-gallery PRIVATE SIGNALS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
signals_configure_target(signals-gallery)
