include(${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake)
signals_embed_tree(${PROJECT_SOURCE_DIR}/resources/learn/course ${CMAKE_CURRENT_BINARY_DIR}/course.cpp kSignalsCourse)

add_library(signals_learn STATIC
  variant.cpp
  task.cpp
  text.cpp
  measure.cpp
  structure.cpp
  answer.cpp
  checker.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/course.cpp
)
target_include_directories(signals_learn PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_learn PUBLIC signals_core signals_pipeline signals_theory PRIVATE signals_i18n tomlplusplus::tomlplusplus fmt::fmt)
signals_configure_target(signals_learn)
