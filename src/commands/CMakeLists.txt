add_library(signals_cli STATIC
  cli.cpp
  cli_docs.cpp
  suites.cpp
  errors.cpp
  audio_commands.cpp
  analysis_commands.cpp
  pipeline_commands.cpp
  run_report.cpp
  watch.cpp
  learn_commands.cpp
)
target_link_libraries(signals_cli PUBLIC signals_core signals_io signals_i18n signals_pipeline PRIVATE signals_svg signals_theory signals_learn CLI11::CLI11 nlohmann_json::nlohmann_json fmt::fmt)
signals_configure_target(signals_cli)
