add_library(signals_pipeline STATIC
  value.cpp
  registry.cpp
  blocks_basic.cpp
  blocks_dsp.cpp
  sig.cpp
  graph.cpp
  cache.cpp
  executor.cpp
)
target_include_directories(signals_pipeline PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_pipeline PUBLIC signals_core signals_io PRIVATE signals_i18n signals_taskflow fmt::fmt)
signals_configure_target(signals_pipeline)
