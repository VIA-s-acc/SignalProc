#include "core/design/elliptic.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace signals::core {

namespace {

constexpr double kMachEp = 1.11022302462515654042e-16;

double agm(double a, double b) {
    for (int i = 0; i < 64 && std::abs(a - b) > kMachEp * std::abs(a); ++i) {
        const double next = (a + b) / 2;
        b = std::sqrt(a * b);
        a = next;
    }
    return (a + b) / 2;
}

}  // namespace

double ellipk(double m) {
    if (m == 1) return std::numeric_limits<double>::infinity();
    if (m > 1 || std::isnan(m)) return std::numeric_limits<double>::quiet_NaN();
    return std::numbers::pi / (2 * agm(1.0, std::sqrt(1 - m)));
}

double ellipkm1(double p) {
    if (p == 0) return std::numeric_limits<double>::infinity();
    if (p < 0 || std::isnan(p)) return std::numeric_limits<double>::quiet_NaN();
    return std::numbers::pi / (2 * agm(1.0, std::sqrt(p)));
}

Jacobi ellipj(double u, double m) {
    Jacobi out;
    if (m < 0 || m > 1 || std::isnan(m)) return {NAN, NAN, NAN};
    if (m < 1.0e-9) {
        const double t = std::sin(u), b = std::cos(u), ai = 0.25 * m * (u - t * b);
        return {t - ai * b, b + ai * t, 1.0 - 0.5 * m * t * t};
    }
    if (m >= 0.9999999999) {
        double ai = 0.25 * (1.0 - m);
        const double b = std::cosh(u), t = std::tanh(u), phi = 1.0 / b, twon = b * std::sinh(u);
        out.sn = t + ai * (twon - u) / (b * b);
        ai *= t * phi;
        out.cn = phi - ai * (twon - u);
        out.dn = phi + ai * (twon + u);
        return out;
    }
    std::array<double, 9> a{}, c{};
    a[0] = 1.0;
    double b = std::sqrt(1.0 - m), twon = 1.0;
    c[0] = std::sqrt(m);
    std::size_t i = 0;
    while (std::abs(c[i] / a[i]) > kMachEp && i < 8) {
        const double ai = a[i];
        ++i;
        c[i] = (ai - b) / 2.0;
        const double t = std::sqrt(ai * b);
        a[i] = (ai + b) / 2.0;
        b = t;
        twon *= 2.0;
    }
    double phi = twon * a[i] * u, prev = phi;
    do {
        const double t = c[i] * std::sin(phi) / a[i];
        prev = phi;
        phi = (std::asin(t) + phi) / 2.0;
    } while (--i);
    out.sn = std::sin(phi);
    out.cn = std::cos(phi);
    out.dn = out.cn / std::cos(phi - prev);
    return out;
}

Result<double> ellipdeg(int n, double m1) {
    constexpr int kMax = 7;
    const double k1 = ellipk(m1), k1p = ellipkm1(m1);
    if (!std::isfinite(k1) || !std::isfinite(k1p) || n < 1) return make_error(Errc::invalid_argument, "ellipdeg: parameter out of range");
    const double q = std::pow(std::exp(-std::numbers::pi * k1p / k1), 1.0 / n);
    double num = 0, den = 1;
    for (int m = 0; m <= kMax; ++m) num += std::pow(q, m * (m + 1));
    for (int m = 1; m <= kMax + 1; ++m) den += 2 * std::pow(q, m * m);
    return 16 * q * std::pow(num / den, 4);
}

Result<double> arc_jac_sc1(double w, double m) {
    using C = std::complex<double>;
    const double k = std::sqrt(m);
    if (k > 1) return make_error(Errc::invalid_argument, "arc_jac_sc1: m > 1");
    auto complement = [](C kx) { return std::sqrt((1.0 - kx) * (1.0 + kx)); };
    const C wc{0, w};
    C z;
    if (k == 1) {
        z = std::atanh(wc);
    } else {
        std::vector<double> ks{k};
        while (ks.back() != 0) {
            const double kp = std::sqrt((1 - ks.back()) * (1 + ks.back()));
            ks.push_back((1 - kp) / (1 + kp));
            if (ks.size() > 11) return make_error(Errc::no_convergence, "arc_jac_sc1: Landen transformation not converging");
        }
        double big_k = std::numbers::pi / 2;
        for (std::size_t i = 1; i < ks.size(); ++i) big_k *= 1 + ks[i];
        C wn = wc;
        for (std::size_t i = 0; i + 1 < ks.size(); ++i) wn = 2.0 * wn / ((1 + ks[i + 1]) * (1.0 + complement(ks[i] * wn)));
        z = big_k * (2 / std::numbers::pi) * std::asin(wn);
    }
    if (std::abs(z.real()) > 1e-14) return make_error(Errc::no_convergence, "arc_jac_sc1: result is not imaginary");
    return z.imag();
}

}  // namespace signals::core
