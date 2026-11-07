#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

#include "core/cwt.hpp"
#include "core/error.hpp"
#include "core/laurent.hpp"

namespace signals::core {

std::size_t spectrum_factors(double omega_max);

Result<std::vector<std::complex<double>>> scaling_spectrum(const Laurent& lowpass, std::span<const double> omega, std::size_t factors = 0);
Result<std::vector<std::complex<double>>> wavelet_spectrum(const Laurent& lowpass, const Laurent& highpass, std::span<const double> omega,
                                                           std::size_t factors = 0);
std::vector<std::complex<double>> continuous_spectrum(const ContinuousWavelet& w, std::span<const double> omega);

}  // namespace signals::core
