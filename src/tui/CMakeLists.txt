include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/themes/dark.toml ${CMAKE_CURRENT_BINARY_DIR}/theme_dark.cpp kSignalsThemeDark)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/themes/light.toml ${CMAKE_CURRENT_BINARY_DIR}/theme_light.cpp kSignalsThemeLight)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/theory/explain/haar.toml ${CMAKE_CURRENT_BINARY_DIR}/explain_haar.cpp kSignalsExplainHaar)

add_library(signals_tui STATIC
  run.cpp
  widgets/format.cpp
  widgets/screen_util.cpp
  widgets/bank_diagram.cpp
  widgets/plot.cpp
  widgets/heatmap.cpp
  widgets/zplane.cpp
  theme/theme.cpp
  term/caps.cpp
  data/pyramid.cpp
  data/user_filters.cpp
  jobs/jobs.cpp
  monitor/sampler_common.cpp
  monitor/monitor.cpp
  shell/layout.cpp
  shell/panel.cpp
  shell/shell.cpp
  shell/status.cpp
  shell/help.cpp
  shell/placeholder.cpp
  shell/palette.cpp
  shell/overlays.cpp
  shell/ticker.cpp
  shell/subtabs.cpp
  shell/errors.cpp
  screens/signal_model.cpp
  screens/signal_screen.cpp
  screens/filters_model.cpp
  screens/filters_screen.cpp
  screens/explain_model.cpp
  screens/explain_screen.cpp
  screens/designer_model.cpp
  screens/designer_screen.cpp
  screens/decomposition_model.cpp
  screens/decomposition_screen.cpp
  screens/matrix_model.cpp
  term/check_screen.cpp
  monitor/resources_panel.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theme_dark.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theme_light.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/explain_haar.cpp
)
target_include_directories(signals_tui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_tui
  PUBLIC signals_core signals_io signals_audio signals_i18n ftxui::component ftxui::dom ftxui::screen
  PRIVATE tomlplusplus::tomlplusplus fmt::fmt)
signals_configure_target(signals_tui)
if(APPLE)
  target_sources(signals_tui PRIVATE monitor/sampler_macos.cpp)
else()
  target_sources(signals_tui PRIVATE monitor/sampler_linux.cpp)
endif()
