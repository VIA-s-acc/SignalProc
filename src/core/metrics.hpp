#pragma once

#include <span>

namespace signals::core {

double neumaier_sum(std::span<const double> x);
double energy(std::span<const double> x);
double max_abs_error(std::span<const double> a, std::span<const double> b);
double rms_error(std::span<const double> a, std::span<const double> b);
double relative_l2_error(std::span<const double> reference, std::span<const double> test);
double snr_db(std::span<const double> reference, std::span<const double> test);

}  // namespace signals::core
