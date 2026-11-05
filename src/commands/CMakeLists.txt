include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/i18n/ru.json ${CMAKE_CURRENT_BINARY_DIR}/i18n_ru.cpp kSignalsRuJson)

add_library(signals_cli STATIC
  cli.cpp
  i18n.cpp
  suites.cpp
  errors.cpp
  audio_commands.cpp
  analysis_commands.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/i18n_ru.cpp
)
target_link_libraries(signals_cli PUBLIC signals_core signals_io PRIVATE CLI11::CLI11 nlohmann_json::nlohmann_json fmt::fmt)
signals_configure_target(signals_cli)
