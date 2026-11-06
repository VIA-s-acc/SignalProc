add_library(signals_cli STATIC
  cli.cpp
  suites.cpp
  errors.cpp
  audio_commands.cpp
  analysis_commands.cpp
  pipeline_commands.cpp
)
target_link_libraries(signals_cli PUBLIC signals_core signals_io signals_i18n signals_pipeline PRIVATE CLI11::CLI11 nlohmann_json::nlohmann_json fmt::fmt)
signals_configure_target(signals_cli)
