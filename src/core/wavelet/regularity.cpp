#include "core/wavelet/regularity.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <Eigen/Eigenvalues>

namespace signals::core {
namespace {

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

double sobolev_from_autocorrelation(const Laurent& l_squared, int zeros) {
    const auto k = -l_squared.first();
    const auto dim = 2 * k + 1;
    Eigen::MatrixXd t = Eigen::MatrixXd::Zero(dim, dim);
    for (std::ptrdiff_t row = -k; row <= k; ++row)
        for (std::ptrdiff_t col = -k; col <= k; ++col) t(row + k, col + k) = 2.0 * l_squared.at(2 * row - col);
    const Eigen::EigenSolver<Eigen::MatrixXd> solver(t, false);
    double rho = 0.0;
    for (const auto& lambda : solver.eigenvalues()) rho = std::max(rho, std::abs(lambda));
    return static_cast<double>(zeros) - std::log(rho) / std::log(4.0);
}

namespace {

std::optional<std::vector<double>> normalised(const Laurent& h) {
    std::vector<double> c = trim(h, 0.0).coeffs;
    double sum = 0.0;
    for (double v : c) sum += v;
    if (c.empty() || std::abs(sum) < 1e-300) return std::nullopt;
    for (auto& v : c) v /= sum;
    return c;
}

double from_reduced(const std::vector<double>& reduced, int zeros) {
    const Laurent l{reduced, 0};
    return sobolev_from_autocorrelation(multiply(l, reverse(l)), zeros);
}

}  // namespace

double sobolev_exponent(const Laurent& h, int zeros) {
    auto c = normalised(h);
    if (!c) return std::numeric_limits<double>::quiet_NaN();
    for (int i = 0; i < zeros && c->size() > 1; ++i) {
        std::vector<double> q(c->size() - 1);
        double carry = 0.0;
        for (std::size_t j = c->size(); j-- > 1;) {
            carry = (*c)[j] - carry;
            q[j - 1] = 2.0 * carry;
        }
        *c = std::move(q);
    }
    return from_reduced(*c, zeros);
}

double sobolev_exponent(const Laurent& h) {
    auto c = normalised(h);
    if (!c) return std::numeric_limits<double>::quiet_NaN();
    double peak = 0.0;
    for (double v : *c) peak = std::max(peak, std::abs(v));
    int n = 0;
    while (auto q = divide_by_one_plus_w(*c, 1e-9, peak)) {
        *c = std::move(*q);
        for (auto& v : *c) v *= 2.0;
        ++n;
    }
    return from_reduced(*c, n);
}

}  // namespace signals::core
