include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/themes/dark.toml ${CMAKE_CURRENT_BINARY_DIR}/theme_dark.cpp kSignalsThemeDark)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/themes/light.toml ${CMAKE_CURRENT_BINARY_DIR}/theme_light.cpp kSignalsThemeLight)

add_library(signals_tui STATIC
  run.cpp
  widgets/format.cpp
  widgets/plot.cpp
  widgets/heatmap.cpp
  widgets/zplane.cpp
  theme/theme.cpp
  term/caps.cpp
  data/pyramid.cpp
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
  term/check_screen.cpp
  monitor/resources_panel.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theme_dark.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theme_light.cpp
)
target_include_directories(signals_tui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_tui
  PUBLIC signals_core signals_io signals_i18n ftxui::component ftxui::dom ftxui::screen
  PRIVATE tomlplusplus::tomlplusplus fmt::fmt)
signals_configure_target(signals_tui)
if(APPLE)
  target_sources(signals_tui PRIVATE monitor/sampler_macos.cpp)
else()
  target_sources(signals_tui PRIVATE monitor/sampler_linux.cpp)
endif()
