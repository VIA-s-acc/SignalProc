add_library(signals_io STATIC
  wav.cpp
)
target_link_libraries(signals_io PUBLIC signals_core PRIVATE signals_dr_wav)
signals_configure_target(signals_io)
