add_library(signals_core STATIC
  version.cpp
  laurent.cpp
  conv.cpp
  extension.cpp
  metrics.cpp
  memory.cpp
  thread_name.cpp
  spectral.cpp
  signal_ops.cpp
  multirate.cpp
  freq.cpp
  process/threshold.cpp
  process/shrink.cpp
  check.cpp
  bank.cpp
  wavedec.cpp
  swt.cpp
  lifting.cpp
  packets.cpp
  cwt.cpp
  design/maxflat.cpp
  design/split.cpp
  design/tables.cpp
  design/daubechies.cpp
  design/families.cpp
  design/window_fir.cpp
  design/elliptic.cpp
  design/iir.cpp
  design/fir.cpp
  matrix.cpp
  wavelet/regularity.cpp
  wavelet/cascade.cpp
)
set_property(SOURCE version.cpp APPEND PROPERTY COMPILE_DEFINITIONS SIGNALS_VERSION="${PROJECT_VERSION}${SIGNALS_VERSION_SUFFIX}")
target_include_directories(signals_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(signals_core PUBLIC Eigen3::Eigen signals_multiprecision signals_pocketfft)
signals_configure_target(signals_core)
