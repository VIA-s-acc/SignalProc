add_executable(signals main.cpp)
target_link_libraries(signals PRIVATE signals_cli)
signals_configure_target(signals)
