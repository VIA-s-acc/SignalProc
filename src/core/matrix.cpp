#include "core/matrix.hpp"

#include <algorithm>
#include <cmath>
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

}  // namespace signals::core
