include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/02-theory.md ${CMAKE_CURRENT_BINARY_DIR}/theory_book.cpp kSignalsTheoryBook)

add_library(signals_theory STATIC
  sections.cpp
  latex.cpp
  math_layout.cpp
  text.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theory_book.cpp
)
target_include_directories(signals_theory PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_theory PUBLIC signals_core)
signals_configure_target(signals_theory)
