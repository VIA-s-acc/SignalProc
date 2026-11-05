#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <Eigen/Dense>

#include "core/check.hpp"
#include "core/dwt.hpp"

namespace signals::core {

Eigen::MatrixXd convolution_matrix(const Laurent& h, std::size_t n);
Eigen::MatrixXd circulant_matrix(const Laurent& h, std::size_t n);
std::vector<double> circulant_multiply_fft(const Laurent& h, std::span<const double> x);

Eigen::MatrixXd analysis_matrix(const FilterBank& bank, std::size_t n, const DwtOptions& options);
Eigen::MatrixXd synthesis_matrix(const FilterBank& bank, std::size_t n, const DwtOptions& options);
Eigen::MatrixXd multilevel_matrix(const FilterBank& bank, std::size_t n, std::size_t levels, const DwtOptions& options);

CheckResult check_left_inverse(const Eigen::MatrixXd& s, const Eigen::MatrixXd& a, double tol);
CheckResult check_orthogonal_matrix(const Eigen::MatrixXd& a, double tol);

}  // namespace signals::core
