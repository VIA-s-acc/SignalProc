include(Catch)

add_executable(signals_tests
  test_toolchain.cpp
  core/test_laurent.cpp
  core/test_conv.cpp
  core/test_extension.cpp
  core/test_metrics.cpp
  core/test_bank.cpp
  core/test_dwt.cpp
  core/test_wavedec.cpp
  core/test_maxflat.cpp
  core/test_regularity.cpp
  core/test_split.cpp
  core/test_daubechies.cpp
  core/test_families.cpp
  core/test_matrix.cpp
  core/test_exact_reference.cpp
  io/test_wav_read.cpp
  io/test_wav_write.cpp
  io/test_stats.cpp
  io/test_generators.cpp
  cli/test_cli.cpp
  cli/test_cli_commands.cpp
  test_golden_files.cpp
  support/golden.cpp
  support/banks.cpp
)
target_link_libraries(signals_tests PRIVATE signals_cli signals_io Catch2::Catch2WithMain rapidcheck nlohmann_json::nlohmann_json)
target_include_directories(signals_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(signals_tests PRIVATE SIGNALS_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden"
                          SIGNALS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
signals_configure_target(signals_tests)
catch_discover_tests(signals_tests)
