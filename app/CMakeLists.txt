add_executable(signals main.cpp)
target_link_libraries(signals PRIVATE signals_core)
signals_configure_target(signals)
