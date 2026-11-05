#include "core/wavelet/regularity.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <Eigen/Eigenvalues>

namespace signals::core {
namespace {

// Divides c(w) = sum c_k w^k by (1 + w); returns the quotient if the remainder is within tol * peak.
std::optional<std::vector<double>> divide_by_one_plus_w(const std::vector<double>& c, double tol, double peak) {
    if (c.size() < 2) return std::nullopt;
    std::vector<double> q(c.size() - 1);
    double carry = 0.0;
    for (std::size_t i = c.size(); i-- > 1;) {
        carry = c[i] - carry;
        q[i - 1] = carry;
    }
    const double remainder = c[0] - carry;
    if (std::abs(remainder) > tol * peak) return std::nullopt;
    return q;
}

}  // namespace

int zeros_at_minus_one(const Laurent& h, double tol) {
    std::vector<double> c = trim(h, 0.0).coeffs;
    double peak = 0.0;
    for (double v : c) peak = std::max(peak, std::abs(v));
    int count = 0;
    while (auto q = divide_by_one_plus_w(c, tol, peak)) {
        c = std::move(*q);
        ++count;
    }
    return count;
}

double sobolev_exponent(const Laurent& h) {
    std::vector<double> c = trim(h, 0.0).coeffs;
    double sum = 0.0;
    for (double v : c) sum += v;
    if (c.empty() || std::abs(sum) < 1e-300) return std::numeric_limits<double>::quiet_NaN();
    for (auto& v : c) v /= sum;
    double peak = 0.0;
    for (double v : c) peak = std::max(peak, std::abs(v));
    int n = 0;
    while (auto q = divide_by_one_plus_w(c, 1e-9, peak)) {
        c = std::move(*q);
        for (auto& v : c) v *= 2.0;
        ++n;
    }
    const Laurent l{c, 0};
    const auto w = multiply(l, reverse(l));
    const auto k = static_cast<std::ptrdiff_t>(c.size()) - 1;
    const auto dim = 2 * k + 1;
    Eigen::MatrixXd t = Eigen::MatrixXd::Zero(dim, dim);
    for (std::ptrdiff_t row = -k; row <= k; ++row)
        for (std::ptrdiff_t col = -k; col <= k; ++col) t(row + k, col + k) = 2.0 * w.at(2 * row - col);
    const Eigen::EigenSolver<Eigen::MatrixXd> solver(t, false);
    double rho = 0.0;
    for (const auto& lambda : solver.eigenvalues()) rho = std::max(rho, std::abs(lambda));
    return static_cast<double>(n) - std::log(rho) / std::log(4.0);
}

}  // namespace signals::core
