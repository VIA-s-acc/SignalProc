include(Catch)

add_executable(signals_tests
  test_toolchain.cpp
  core/test_laurent.cpp
  core/test_conv.cpp
  core/test_extension.cpp
  cli/test_cli.cpp
  test_golden_files.cpp
  support/golden.cpp
)
target_link_libraries(signals_tests PRIVATE signals_cli Catch2::Catch2WithMain rapidcheck nlohmann_json::nlohmann_json)
target_include_directories(signals_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(signals_tests PRIVATE SIGNALS_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")
signals_configure_target(signals_tests)
catch_discover_tests(signals_tests)
