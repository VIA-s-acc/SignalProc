#pragma once

#include <span>
#include <vector>

namespace signals::core {

template <class T>
struct BasicCoeffs;

double neumaier_sum(std::span<const double> x);
double energy(std::span<const double> x);
double max_abs_error(std::span<const double> a, std::span<const double> b);
double rms_error(std::span<const double> a, std::span<const double> b);
double relative_l2_error(std::span<const double> reference, std::span<const double> test);
double snr_db(std::span<const double> reference, std::span<const double> test);
double psnr_db(std::span<const double> reference, std::span<const double> test, double peak);
std::vector<double> band_energy_fractions(const BasicCoeffs<double>& coeffs);
std::vector<double> band_energy_fractions(const BasicCoeffs<double>& coeffs, std::span<const double> weights);

}  // namespace signals::core
