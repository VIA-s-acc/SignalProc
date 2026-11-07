signals_embed_dir(${PROJECT_SOURCE_DIR}/examples "*.sig" ${CMAKE_CURRENT_BINARY_DIR}/examples_embedded.cpp kSignalsExamples)

add_library(signals_pipeline STATIC
  examples.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/examples_embedded.cpp
  docs.cpp
  value.cpp
  registry.cpp
  blocks_basic.cpp
  blocks_dsp.cpp
  blocks_level.cpp
  blocks_sources.cpp
  blocks_filter.cpp
  blocks_wavelet.cpp
  blocks_channels.cpp
  blocks_measure.cpp
  blocks_restore.cpp
  sig.cpp
  graph.cpp
  cache.cpp
  executor.cpp
  layout.cpp
)
target_include_directories(signals_pipeline PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_pipeline PUBLIC signals_core signals_io PRIVATE signals_i18n signals_taskflow fmt::fmt nlohmann_json::nlohmann_json)
signals_configure_target(signals_pipeline)
