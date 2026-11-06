#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <Eigen/Dense>

#include "core/check.hpp"
#include "core/dwt.hpp"
#include "core/error.hpp"

namespace signals::core {

Eigen::MatrixXd convolution_matrix(const Laurent& h, std::size_t n);
Eigen::MatrixXd circulant_matrix(const Laurent& h, std::size_t n);
std::vector<double> circulant_multiply_fft(const Laurent& h, std::span<const double> x);
CheckResult check_circulant_fft(const Laurent& h, std::size_t n, double tol);

Result<Eigen::MatrixXd> analysis_matrix(const FilterBank& bank, std::size_t n, const DwtOptions& options);
Result<Eigen::MatrixXd> synthesis_matrix(const FilterBank& bank, std::size_t n, const DwtOptions& options);
Result<Eigen::MatrixXd> multilevel_matrix(const FilterBank& bank, std::size_t n, std::size_t levels,
                                          const DwtOptions& options);

std::vector<double> singular_values(const Eigen::MatrixXd& a);
double condition_number(std::span<const double> singular);
CheckResult check_left_inverse(const Eigen::MatrixXd& s, const Eigen::MatrixXd& a, double tol);
CheckResult check_orthogonal_matrix(const Eigen::MatrixXd& a, double tol);

}  // namespace signals::core
