add_library(signals_io STATIC
  wav.cpp
  audio_file.cpp
  temp_file.cpp
  stats.cpp
  generators.cpp
  config_dir.cpp
  npy.cpp
  npy_audio.cpp
)
target_link_libraries(signals_io PUBLIC signals_core PRIVATE signals_dr_libs signals_miniz)
signals_configure_target(signals_io)
