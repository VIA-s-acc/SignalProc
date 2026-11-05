add_library(signals_tui STATIC
  run.cpp
)
target_include_directories(signals_tui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_tui
  PUBLIC signals_core signals_io signals_i18n ftxui::component ftxui::dom ftxui::screen
  PRIVATE tomlplusplus::tomlplusplus)
signals_configure_target(signals_tui)
