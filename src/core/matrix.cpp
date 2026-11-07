#include "core/matrix.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>
#include <complex>

#include "core/detail/pocketfft.hpp"

#include "core/wavedec.hpp"

namespace signals::core {
namespace {

std::vector<double> unit(std::size_t n, std::size_t j) {
    std::vector<double> e(n, 0.0);
    e[j] = 1.0;
    return e;
}

Eigen::Index idx(std::size_t i) { return static_cast<Eigen::Index>(i); }

}  // namespace

Eigen::MatrixXd convolution_matrix(const Laurent& h, std::size_t n) {
    const std::size_t taps = h.coeffs.size();
    Eigen::MatrixXd m = Eigen::MatrixXd::Zero(idx(n + taps - 1), idx(n));
    for (std::size_t j = 0; j < n; ++j)
        for (std::size_t k = 0; k < taps; ++k) m(idx(j + k), idx(j)) = h.coeffs[k];
    return m;
}

Eigen::MatrixXd circulant_matrix(const Laurent& h, std::size_t n) {
    if (n == 0) return {};
    Eigen::MatrixXd c = Eigen::MatrixXd::Zero(idx(n), idx(n));
    const auto size = static_cast<std::ptrdiff_t>(n);
    for (std::size_t k = 0; k < h.coeffs.size(); ++k) {
        const auto shift = detail::floor_mod(h.offset + static_cast<std::ptrdiff_t>(k), size);
        for (std::ptrdiff_t j = 0; j < size; ++j) c((j + shift) % size, j) += h.coeffs[k];
    }
    return c;
}

std::vector<double> circulant_multiply_fft(const Laurent& h, std::span<const double> x) {
    const std::size_t n = x.size();
    if (n == 0) return {};
    std::vector<double> wrapped(n, 0.0);
    for (std::size_t k = 0; k < h.coeffs.size(); ++k)
        wrapped[static_cast<std::size_t>(
            detail::floor_mod(h.offset + static_cast<std::ptrdiff_t>(k), static_cast<std::ptrdiff_t>(n)))] +=
            h.coeffs[k];
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t real_stride{sizeof(double)};
    const pocketfft::stride_t complex_stride{sizeof(std::complex<double>)};
    std::vector<std::complex<double>> fh(n / 2 + 1), fx(n / 2 + 1);
    pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, wrapped.data(), fh.data(), 1.0);
    pocketfft::r2c(shape, real_stride, complex_stride, 0, pocketfft::FORWARD, x.data(), fx.data(), 1.0);
    for (std::size_t k = 0; k < fh.size(); ++k) fh[k] *= fx[k];
    std::vector<double> y(n);
    pocketfft::c2r(shape, complex_stride, real_stride, 0, pocketfft::BACKWARD, fh.data(), y.data(),
                   1.0 / static_cast<double>(n));
    return y;
}

Result<Eigen::MatrixXd> analysis_matrix(const FilterBank& bank, std::size_t n, const DwtOptions& options) {
    if (n == 0) return make_error(Errc::invalid_argument, "analysis_matrix: empty signal");
    const std::size_t len = dwt_coeff_len(n, bank.length(), options.mode);
    Eigen::MatrixXd a = Eigen::MatrixXd::Zero(idx(2 * len), idx(n));
    for (std::size_t j = 0; j < n; ++j) {
        const auto e = unit(n, j);
        const auto level = dwt_level<double>(e, bank, options);
        if (!level) return std::unexpected(level.error());
        for (std::size_t i = 0; i < len; ++i) {
            a(idx(i), idx(j)) = level->approx[i];
            a(idx(len + i), idx(j)) = level->detail[i];
        }
    }
    return a;
}

Result<Eigen::MatrixXd> synthesis_matrix(const FilterBank& bank, std::size_t n, const DwtOptions& options) {
    if (n == 0) return make_error(Errc::invalid_argument, "synthesis_matrix: empty signal");
    const std::size_t len = dwt_coeff_len(n, bank.length(), options.mode);
    Eigen::MatrixXd s = Eigen::MatrixXd::Zero(idx(n), idx(2 * len));
    for (std::size_t c = 0; c < 2 * len; ++c) {
        std::vector<double> approx(len, 0.0), detail(len, 0.0);
        (c < len ? approx[c] : detail[c - len]) = 1.0;
        const auto rec = idwt_level<double>(approx, detail, bank, options);
        if (!rec) return std::unexpected(rec.error());
        for (std::size_t i = 0; i < n && i < rec->size(); ++i) s(idx(i), idx(c)) = (*rec)[i];
    }
    return s;
}

Result<Eigen::MatrixXd> multilevel_matrix(const FilterBank& bank, std::size_t n, std::size_t levels,
                                          const DwtOptions& options) {
    if (n == 0) return make_error(Errc::invalid_argument, "multilevel_matrix: empty signal");
    Eigen::MatrixXd w;
    for (std::size_t j = 0; j < n; ++j) {
        const auto e = unit(n, j);
        const auto coeffs = wavedec<double>(e, bank, levels, options);
        if (!coeffs) return std::unexpected(coeffs.error());
        std::vector<double> column;
        for (const auto& band : coeffs->bands) column.insert(column.end(), band.begin(), band.end());
        if (j == 0) w = Eigen::MatrixXd::Zero(idx(column.size()), idx(n));
        for (std::size_t i = 0; i < column.size(); ++i) w(idx(i), idx(j)) = column[i];
    }
    return w;
}

CheckResult check_left_inverse(const Eigen::MatrixXd& s, const Eigen::MatrixXd& a, double tol) {
    if (s.size() == 0 || a.size() == 0 || s.cols() != a.rows()) return {"check.sa_identity", "", INFINITY, tol};
    const Eigen::MatrixXd product = s * a;
    const double error = product.allFinite()
                             ? (product - Eigen::MatrixXd::Identity(product.rows(), product.cols())).cwiseAbs().maxCoeff()
                             : std::nan("");
    return {"check.sa_identity", "", error, tol};
}

CheckResult check_orthogonal_matrix(const Eigen::MatrixXd& a, double tol) {
    if (a.size() == 0) return {"check.ata_identity", "", INFINITY, tol};
    const Eigen::MatrixXd gram = a.transpose() * a;
    const double error = gram.allFinite()
                             ? (gram - Eigen::MatrixXd::Identity(gram.rows(), gram.cols())).cwiseAbs().maxCoeff()
                             : std::nan("");
    return {"check.ata_identity", "", error, tol};
}

std::vector<double> singular_values(const Eigen::MatrixXd& a) {
    if (a.size() == 0) return {};
    const Eigen::JacobiSVD<Eigen::MatrixXd> svd(a);
    const auto& s = svd.singularValues();
    return {s.data(), s.data() + s.size()};
}

double condition_number(std::span<const double> singular) {
    if (singular.empty()) return std::numeric_limits<double>::infinity();
    const auto [lo, hi] = std::minmax_element(singular.begin(), singular.end());
    return *lo > 0 ? *hi / *lo : std::numeric_limits<double>::infinity();
}

CheckResult check_circulant_fft(const Laurent& h, std::size_t n, double tol) {
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i) x[i] = std::sin(0.7 * static_cast<double>(i)) + 0.25 * std::cos(2.3 * static_cast<double>(i));
    const Eigen::MatrixXd c = circulant_matrix(h, n);
    const Eigen::VectorXd direct = c * Eigen::Map<const Eigen::VectorXd>(x.data(), static_cast<Eigen::Index>(n));
    const auto fast = circulant_multiply_fft(h, x);
    double worst = fast.size() == n ? 0.0 : std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < n && i < fast.size(); ++i) {
        const double diff = std::abs(direct(static_cast<Eigen::Index>(i)) - fast[i]);
        worst = std::isnan(diff) || std::isnan(worst) ? std::numeric_limits<double>::quiet_NaN() : std::max(worst, diff);
    }
    return {"check.circulant_fft", "h", worst, tol};
}

Eigen::MatrixXd symmetric_convolution_matrix(const Laurent& h, std::size_t n) {
    Eigen::MatrixXd a = Eigen::MatrixXd::Zero(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(n));
    if (n == 0) return a;
    const auto size = static_cast<std::ptrdiff_t>(n);
    auto reflect = [size](std::ptrdiff_t m) {
        while (m < 0 || m >= size) m = m < 0 ? -m - 1 : 2 * size - 1 - m;
        return m;
    };
    for (std::ptrdiff_t i = 0; i < size; ++i)
        for (std::size_t k = 0; k < h.coeffs.size(); ++k) {
            const auto m = reflect(i - (h.offset + static_cast<std::ptrdiff_t>(k)));
            a(i, m) += h.coeffs[k];
        }
    return a;
}

Eigen::MatrixXd dct2_matrix(std::size_t n) {
    Eigen::MatrixXd c(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(n));
    for (std::size_t k = 0; k < n; ++k) {
        const double scale = std::sqrt((k == 0 ? 1.0 : 2.0) / static_cast<double>(n));
        for (std::size_t j = 0; j < n; ++j)
            c(static_cast<Eigen::Index>(k), static_cast<Eigen::Index>(j)) =
                scale * std::cos(std::numbers::pi * static_cast<double>(k) * (2.0 * static_cast<double>(j) + 1) / (2.0 * static_cast<double>(n)));
    }
    return c;
}

double diagonalisation_error(const Eigen::MatrixXd& a, const Eigen::MatrixXd& basis) {
    Eigen::MatrixXd d = basis * a * basis.transpose();
    d.diagonal().setZero();
    return d.size() ? d.cwiseAbs().maxCoeff() : 0.0;
}

Result<Levinson> levinson_solve(std::span<const double> r, std::span<const double> b) {
    const auto n = r.size();
    if (n == 0 || b.size() != n) return make_error(Errc::length_mismatch, "levinson: r and b need the same nonzero length");
    if (std::abs(r[0]) < 1e-300) return make_error(Errc::invalid_argument, "levinson: r[0] = 0, the first leading minor is singular");
    Levinson out;
    std::vector<double> f{1 / r[0]}, x{b[0] / r[0]};
    for (std::size_t k = 1; k < n; ++k) {
        double ef = 0, ex = 0;
        for (std::size_t i = 0; i < k; ++i) {
            ef += r[k - i] * f[i];
            ex += r[k - i] * x[i];
        }
        const double denom = 1 - ef * ef;
        if (std::abs(denom) < 1e-12) return make_error(Errc::invalid_argument, "levinson: a leading minor is singular (reflection coefficient ±1)");
        std::vector<double> next(k + 1, 0.0);
        for (std::size_t i = 0; i < k; ++i) {
            next[i] += f[i] / denom;
            next[i + 1] -= ef * f[k - 1 - i] / denom;
        }
        f = std::move(next);
        x.push_back(0.0);
        for (std::size_t i = 0; i <= k; ++i) x[i] += (b[k] - ex) * f[k - i];
        out.reflection.push_back(-ef);
    }
    out.x = std::move(x);
    return out;
}

}  // namespace signals::core
