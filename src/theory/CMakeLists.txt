include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/02-theory.md ${CMAKE_CURRENT_BINARY_DIR}/theory_book.cpp kSignalsTheoryBook)
signals_embed_dir(${PROJECT_SOURCE_DIR}/resources/theory/blocks "*.md" ${CMAKE_CURRENT_BINARY_DIR}/theory_cards.cpp kSignalsTheoryCards)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/02-theory.en.md ${CMAKE_CURRENT_BINARY_DIR}/theory_book_en.cpp kSignalsTheoryBookEn)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/user/sig-format.md ${CMAKE_CURRENT_BINARY_DIR}/sig_format.cpp kSignalsSigFormat)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/user/sig-format.en.md ${CMAKE_CURRENT_BINARY_DIR}/sig_format_en.cpp kSignalsSigFormatEn)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/user/config.md ${CMAKE_CURRENT_BINARY_DIR}/settings_doc.cpp kSignalsSettings)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/user/config.en.md ${CMAKE_CURRENT_BINARY_DIR}/settings_doc_en.cpp kSignalsSettingsEn)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/user/learn.md ${CMAKE_CURRENT_BINARY_DIR}/learn_doc.cpp kSignalsLearn)
signals_embed_text(${PROJECT_SOURCE_DIR}/docs/user/learn.en.md ${CMAKE_CURRENT_BINARY_DIR}/learn_doc_en.cpp kSignalsLearnEn)
signals_embed_dir(${PROJECT_SOURCE_DIR}/resources/theory/en/blocks "*.md" ${CMAKE_CURRENT_BINARY_DIR}/theory_cards_en.cpp kSignalsTheoryCardsEn)

file(GLOB card_files CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/theory/blocks/*.md)
set(card_sources "")
foreach(card ${card_files})
  file(STRINGS ${card} source_line REGEX "^source = \"[^\"]+\"" LIMIT_COUNT 1)
  string(REGEX REPLACE "^source = \"([^\"]+)\".*" "\\1" source_path "${source_line}")
  if(source_path)
    list(APPEND card_sources ${source_path})
  endif()
endforeach()
list(REMOVE_DUPLICATES card_sources)
list(SORT card_sources)
signals_embed_sources(${PROJECT_SOURCE_DIR} "${card_sources}" ${CMAKE_CURRENT_BINARY_DIR}/card_sources.cpp kSignalsCardSources)

add_library(signals_theory STATIC
  sections.cpp
  latex.cpp
  math_layout.cpp
  markdown.cpp
  live.cpp
  blocks.cpp
  cards.cpp
  text.cpp
  sources.cpp
  pipeline_cards.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theory_book.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theory_cards.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theory_book_en.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/sig_format.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/sig_format_en.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/settings_doc.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/settings_doc_en.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/learn_doc.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/learn_doc_en.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/theory_cards_en.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/card_sources.cpp
)
target_include_directories(signals_theory PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_theory PUBLIC signals_core PRIVATE signals_i18n tomlplusplus::tomlplusplus fmt::fmt)
signals_configure_target(signals_theory)
