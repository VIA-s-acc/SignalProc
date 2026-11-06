include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/i18n/ru.json ${CMAKE_CURRENT_BINARY_DIR}/i18n_ru.cpp kSignalsRuJson)
signals_embed_text(${PROJECT_SOURCE_DIR}/resources/i18n/en.json ${CMAKE_CURRENT_BINARY_DIR}/i18n_en.cpp kSignalsEnJson)

add_library(signals_i18n STATIC
  i18n.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/i18n_ru.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/i18n_en.cpp
)
target_include_directories(signals_i18n PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_i18n PRIVATE nlohmann_json::nlohmann_json)
signals_configure_target(signals_i18n)
