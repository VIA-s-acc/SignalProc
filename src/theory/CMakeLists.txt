include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/02-theory.md ${CMAKE_CURRENT_BINARY_DIR}/theory_book.cpp kSignalsTheoryBook)
signals_embed_dir(${PROJECT_SOURCE_DIR}/resources/theory/blocks "*.md" ${CMAKE_CURRENT_BINARY_DIR}/theory_cards.cpp kSignalsTheoryCards)

add_library(signals_theory STATIC
  sections.cpp
  latex.cpp
  math_layout.cpp
  markdown.cpp
  live.cpp
  blocks.cpp
  cards.cpp
  text.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theory_book.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theory_cards.cpp
)
target_include_directories(signals_theory PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_theory PUBLIC signals_core PRIVATE signals_i18n tomlplusplus::tomlplusplus fmt::fmt)
signals_configure_target(signals_theory)
