include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/themes/dark.toml ${CMAKE_CURRENT_BINARY_DIR}/theme_dark.cpp kSignalsThemeDark)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/themes/light.toml ${CMAKE_CURRENT_BINARY_DIR}/theme_light.cpp kSignalsThemeLight)

add_library(signals_tui STATIC
  run.cpp
  widgets/format.cpp
  widgets/plot.cpp
  theme/theme.cpp
  term/caps.cpp
  data/pyramid.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theme_dark.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theme_light.cpp
)
target_include_directories(signals_tui PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_tui
  PUBLIC signals_core signals_io signals_i18n ftxui::component ftxui::dom ftxui::screen
  PRIVATE tomlplusplus::tomlplusplus)
signals_configure_target(signals_tui)
