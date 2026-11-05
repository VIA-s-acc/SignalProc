include(Catch)

add_executable(signals_tests
  test_toolchain.cpp
  cli/test_cli.cpp
)
target_link_libraries(signals_tests PRIVATE signals_cli Catch2::Catch2WithMain rapidcheck nlohmann_json::nlohmann_json)
target_compile_definitions(signals_tests PRIVATE SIGNALS_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")
signals_configure_target(signals_tests)
catch_discover_tests(signals_tests)
