signals_embed_dir(${PROJECT_SOURCE_DIR}/resources/python "*.py" ${CMAKE_CURRENT_BINARY_DIR}/python_functions.cpp kSignalsPythonFunctions)

add_library(signals_python STATIC
  emitter.cpp
  registry.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/python_functions.cpp
)
target_include_directories(signals_python PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_python PUBLIC signals_pipeline signals_io signals_core PRIVATE signals_i18n fmt::fmt)
signals_configure_target(signals_python)
