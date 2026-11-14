signals_embed_dir(${PROJECT_SOURCE_DIR}/resources/python "*.py" ${CMAKE_CURRENT_BINARY_DIR}/python_functions.cpp kSignalsPythonFunctions)

add_library(signals_python STATIC
  emitter.cpp
  registry.cpp
  script.cpp
  forms_basic.cpp
  forms_filter.cpp
  forms_wavelet.cpp
  forms_effects.cpp
  forms_analysis.cpp
  module_names.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/python_functions.cpp
)
target_include_directories(signals_python PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_python PUBLIC signals_pipeline signals_io signals_core PRIVATE signals_i18n fmt::fmt nlohmann_json::nlohmann_json)
signals_configure_target(signals_python)

file(STRINGS ${PROJECT_SOURCE_DIR}/tools/requirements-bridge.txt bridge_pins REGEX "^(numpy|scipy|PyWavelets)==")
set(bridge_versions "")
foreach(pin ${bridge_pins})
  string(REPLACE "==" " " pin "${pin}")
  if(bridge_versions)
    string(APPEND bridge_versions ", ")
  endif()
  string(APPEND bridge_versions "${pin}")
endforeach()
target_compile_definitions(signals_python PRIVATE SIGNALS_BRIDGE_VERSIONS="${bridge_versions}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/tools/requirements-bridge.txt)
