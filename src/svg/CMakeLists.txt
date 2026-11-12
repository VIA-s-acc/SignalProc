add_library(signals_svg STATIC
  svg.cpp
)
target_include_directories(signals_svg PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_svg PRIVATE fmt::fmt)
signals_configure_target(signals_svg)
