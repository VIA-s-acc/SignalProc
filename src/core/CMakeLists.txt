add_library(signals_core STATIC
  version.cpp
  laurent.cpp
  conv.cpp
  extension.cpp
  metrics.cpp
  signal_ops.cpp
  multirate.cpp
  freq.cpp
  process/threshold.cpp
  process/shrink.cpp
  check.cpp
  bank.cpp
  wavedec.cpp
  design/maxflat.cpp
  design/split.cpp
  design/tables.cpp
  design/daubechies.cpp
  design/families.cpp
  design/window_fir.cpp
  matrix.cpp
  wavelet/regularity.cpp
)
target_include_directories(signals_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_core PUBLIC Eigen3::Eigen signals_multiprecision signals_pocketfft)
signals_configure_target(signals_core)
