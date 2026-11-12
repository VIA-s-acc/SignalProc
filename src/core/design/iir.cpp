#include "core/design/iir.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

#include "core/design/elliptic.hpp"
#include "core/design/roots.hpp"
#include "core/scalar.hpp"

namespace signals::core {

namespace {

using C = std::complex<double>;
constexpr double kEpsilon = 2e-16;

C prod_neg(std::span<const C> v) {
    C out{1, 0};
    for (const auto& x : v) out *= -x;
    return out;
}

Zpk buttap(int n) {
    Zpk out;
    for (int m = -n + 1; m < n; m += 2) out.p.push_back(-std::exp(C{0, std::numbers::pi * m / (2.0 * n)}));
    return out;
}

Zpk cheb1ap(int n, double rp) {
    Zpk out;
    const double eps = std::sqrt(std::pow(10.0, 0.1 * rp) - 1.0);
    const double mu = 1.0 / n * std::asinh(1 / eps);
    for (int m = -n + 1; m < n; m += 2) out.p.push_back(-std::sinh(C{mu, std::numbers::pi * m / (2.0 * n)}));
    out.k = prod_neg(out.p).real();
    if (n % 2 == 0) out.k /= std::sqrt(1 + eps * eps);
    return out;
}

Zpk cheb2ap(int n, double rs) {
    Zpk out;
    const double de = 1.0 / std::sqrt(std::pow(10.0, 0.1 * rs) - 1);
    const double mu = std::asinh(1.0 / de) / n;
    std::vector<int> ms;
    if (n % 2) {
        for (int m = -n + 1; m < 0; m += 2) ms.push_back(m);
        for (int m = 2; m < n; m += 2) ms.push_back(m);
    } else {
        for (int m = -n + 1; m < n; m += 2) ms.push_back(m);
    }
    for (const int m : ms) out.z.push_back(-std::conj(C{0, 1} / std::sin(m * std::numbers::pi / (2.0 * n))));
    for (int m = -n + 1; m < n; m += 2) {
        const C b = -std::exp(C{0, std::numbers::pi * m / (2.0 * n)});
        out.p.push_back(1.0 / C{std::sinh(mu) * b.real(), std::cosh(mu) * b.imag()});
    }
    out.k = (prod_neg(out.p) / prod_neg(out.z)).real();
    return out;
}

Result<Zpk> ellipap(int n, double rp, double rs) {
    Zpk out;
    if (n == 1) {
        const double p = -std::sqrt(1.0 / std::expm1(std::numbers::ln10 * 0.1 * rp));
        out.p = {p};
        out.k = -p;
        return out;
    }
    const double eps_sq = std::expm1(std::numbers::ln10 * 0.1 * rp);
    const double eps = std::sqrt(eps_sq);
    const double ck1_sq = eps_sq / std::expm1(std::numbers::ln10 * 0.1 * rs);
    if (ck1_sq == 0) return make_error(Errc::invalid_argument, "ellip: cannot design a filter with these rp and rs");
    const double k0 = ellipk(ck1_sq);
    auto m = ellipdeg(n, ck1_sq);
    if (!m) return std::unexpected{m.error()};
    const double capk = ellipk(*m);
    std::vector<Jacobi> j;
    for (int i = 1 - n % 2; i < n; i += 2) j.push_back(ellipj(i * capk / n, *m));
    for (const auto& v : j)
        if (std::abs(v.sn) > kEpsilon) out.z.push_back(C{0, 1.0 / (std::sqrt(*m) * v.sn)});
    const auto upper = out.z.size();
    for (std::size_t i = 0; i < upper; ++i) out.z.push_back(std::conj(out.z[i]));
    auto r = arc_jac_sc1(1. / eps, ck1_sq);
    if (!r) return std::unexpected{r.error()};
    const double v0 = capk * *r / (n * k0);
    const auto sv = ellipj(v0, 1 - *m);
    std::vector<C> p;
    for (const auto& v : j) p.push_back(-(v.cn * v.dn * sv.sn * sv.cn + C{0, 1} * v.sn * sv.dn) / (1 - std::pow(v.dn * sv.sn, 2.0)));
    double norm = 0;
    for (const auto& x : p) norm += std::norm(x);
    out.p = p;
    for (const auto& x : p)
        if (n % 2 == 0 || std::abs(x.imag()) > kEpsilon * std::sqrt(norm)) out.p.push_back(std::conj(x));
    out.k = (prod_neg(out.p) / prod_neg(out.z)).real();
    if (n % 2 == 0) out.k /= std::sqrt(1 + eps_sq);
    return out;
}

Result<Zpk> besselap(int n) {
    std::vector<Float256> coeffs;
    for (int k = 0; k <= n; ++k) {
        Float256 c{1};
        for (int i = n - k + 1; i <= n + k; ++i) c *= i;
        for (int i = 0; i < k; ++i) c /= 2;
        for (int i = 2; i <= k; ++i) c /= i;
        coeffs.push_back(c);
    }
    auto roots = polynomial_roots<Float256>(coeffs);
    if (!roots) return std::unexpected{roots.error()};
    Float256 a_last{1};
    for (int i = n + 1; i <= 2 * n; ++i) a_last *= i;
    for (int i = 0; i < n; ++i) a_last /= 2;
    const double scale = std::pow(10.0, -std::log10(static_cast<double>(a_last)) / n);
    Zpk out;
    for (const auto& r : *roots) out.p.push_back(scale / C{static_cast<double>(r.re), static_cast<double>(r.im)});
    std::vector<C> mirrored(out.p.size());
    for (std::size_t i = 0; i < out.p.size(); ++i) mirrored[i] = out.p[i];
    std::ranges::sort(out.p, [](C a, C b) { return a.real() != b.real() ? a.real() < b.real() : a.imag() < b.imag(); });
    for (std::size_t i = 0; i < out.p.size(); ++i) {
        const auto conj = std::ranges::min_element(out.p, {}, [&](C q) { return std::abs(q - std::conj(out.p[i])); });
        mirrored[i] = (out.p[i] + std::conj(*conj)) / 2.0;
    }
    out.p = mirrored;
    return out;
}

void lp2lp(Zpk& f, double wo) {
    const auto degree = static_cast<int>(f.p.size()) - static_cast<int>(f.z.size());
    for (auto& z : f.z) z *= wo;
    for (auto& p : f.p) p *= wo;
    f.k *= std::pow(wo, degree);
}

void lp2hp(Zpk& f, double wo) {
    const auto degree = f.p.size() - f.z.size();
    const double gain = (prod_neg(f.z) / prod_neg(f.p)).real();
    for (auto& z : f.z) z = wo / z;
    for (auto& p : f.p) p = wo / p;
    f.z.insert(f.z.end(), degree, C{0, 0});
    f.k *= gain;
}

void lp2bp(Zpk& f, double wo, double bw) {
    const auto degree = f.p.size() - f.z.size();
    auto split = [&](std::vector<C>& v) {
        std::vector<C> plus, minus;
        for (auto x : v) {
            x *= bw / 2;
            const auto s = std::sqrt(x * x - wo * wo);
            plus.push_back(x + s);
            minus.push_back(x - s);
        }
        plus.insert(plus.end(), minus.begin(), minus.end());
        v = std::move(plus);
    };
    split(f.z);
    split(f.p);
    f.z.insert(f.z.end(), degree, C{0, 0});
    f.k *= std::pow(bw, static_cast<double>(degree));
}

void lp2bs(Zpk& f, double wo, double bw) {
    const auto degree = f.p.size() - f.z.size();
    const double gain = (prod_neg(f.z) / prod_neg(f.p)).real();
    auto split = [&](std::vector<C>& v) {
        std::vector<C> plus, minus;
        for (auto x : v) {
            x = (bw / 2) / x;
            const auto s = std::sqrt(x * x - wo * wo);
            plus.push_back(x + s);
            minus.push_back(x - s);
        }
        plus.insert(plus.end(), minus.begin(), minus.end());
        v = std::move(plus);
    };
    split(f.z);
    split(f.p);
    f.z.insert(f.z.end(), degree, C{0, wo});
    f.z.insert(f.z.end(), degree, C{0, -wo});
    f.k *= gain;
}

void bilinear(Zpk& f, double fs) {
    const auto degree = f.p.size() - f.z.size();
    const double fs2 = 2 * fs;
    C num{1, 0}, den{1, 0};
    for (auto& z : f.z) {
        num *= fs2 - z;
        z = (fs2 + z) / (fs2 - z);
    }
    for (auto& p : f.p) {
        den *= fs2 - p;
        p = (fs2 + p) / (fs2 - p);
    }
    f.z.insert(f.z.end(), degree, C{-1, 0});
    f.k *= (num / den).real();
}

}  // namespace

Result<Zpk> analog_prototype(IirFamily family, int order, double rp, double rs) {
    if (order < 1 || order > 64) return make_error(Errc::invalid_argument, "iir: order must be in 1..64");
    switch (family) {
    case IirFamily::butterworth: return buttap(order);
    case IirFamily::chebyshev1: return cheb1ap(order, rp);
    case IirFamily::chebyshev2: return cheb2ap(order, rs);
    case IirFamily::elliptic: return ellipap(order, rp, rs);
    case IirFamily::bessel: return besselap(order);
    }
    return make_error(Errc::invalid_argument, "iir: unknown family");
}

Result<Zpk> iir_design(const IirSpec& spec) {
    const bool band = spec.type == BandType::bandpass || spec.type == BandType::bandstop;
    if (spec.wn.size() != (band ? 2U : 1U)) return make_error(Errc::invalid_argument, "iir: need one cutoff for low/high-pass and two for band filters");
    for (const double w : spec.wn)
        if (!(w > 0 && w < 1)) return make_error(Errc::invalid_argument, "iir: cutoffs must lie strictly between 0 and the Nyquist frequency");
    if (band && !(spec.wn[0] < spec.wn[1])) return make_error(Errc::invalid_argument, "iir: band edges must increase");
    auto f = analog_prototype(spec.family, spec.order, spec.rp, spec.rs);
    if (!f) return std::unexpected{f.error()};
    constexpr double fs = 2.0;
    std::vector<double> warped;
    for (const double w : spec.wn) warped.push_back(2 * fs * std::tan(std::numbers::pi * w / fs));
    switch (spec.type) {
    case BandType::lowpass: lp2lp(*f, warped[0]); break;
    case BandType::highpass: lp2hp(*f, warped[0]); break;
    case BandType::bandpass: lp2bp(*f, std::sqrt(warped[0] * warped[1]), warped[1] - warped[0]); break;
    case BandType::bandstop: lp2bs(*f, std::sqrt(warped[0] * warped[1]), warped[1] - warped[0]); break;
    }
    bilinear(*f, fs);
    return f;
}

std::vector<Sos> zpk_to_sos(const Zpk& zpk) {
    std::vector<C> zeros = zpk.z, poles = zpk.p;
    auto take_group = [](std::vector<C>& v, C near) {
        std::vector<C> out;
        if (v.empty()) return out;
        const auto it = std::ranges::min_element(v, {}, [&](C x) { return std::abs(x - near); });
        const C first = *it;
        v.erase(it);
        out.push_back(first);
        if (std::abs(first.imag()) > 1e-12 * std::max(1.0, std::abs(first))) {
            const auto mate = std::ranges::min_element(v, {}, [&](C x) { return std::abs(x - std::conj(first)); });
            if (mate != v.end()) {
                out.push_back(*mate);
                v.erase(mate);
            }
        }
        return out;
    };
    auto quadratic = [](std::span<const C> roots) {
        std::array<double, 3> c{1, 0, 0};
        if (roots.size() == 1) c[1] = -roots[0].real();
        if (roots.size() == 2) {
            c[1] = -(roots[0] + roots[1]).real();
            c[2] = (roots[0] * roots[1]).real();
        }
        return c;
    };
    std::vector<Sos> out;
    while (!poles.empty() || !zeros.empty()) {
        const auto pivot = poles.empty() ? zeros.front() : *std::ranges::min_element(poles, {}, [](C p) { return std::abs(1.0 - std::abs(p)); });
        auto p = take_group(poles, pivot);
        if (p.size() == 1 && !poles.empty() && std::abs(p[0].imag()) <= 1e-12) {
            const auto it = std::ranges::find_if(poles, [](C x) { return std::abs(x.imag()) <= 1e-12; });
            if (it != poles.end()) {
                p.push_back(*it);
                poles.erase(it);
            }
        }
        auto z = take_group(zeros, pivot);
        if (z.size() == 1 && p.size() == 2 && !zeros.empty() && std::abs(z[0].imag()) <= 1e-12) {
            const auto it = std::ranges::min_element(zeros, {}, [&](C x) { return std::abs(x - pivot) + (std::abs(x.imag()) > 1e-12 ? 1e9 : 0.0); });
            if (it != zeros.end() && std::abs(it->imag()) <= 1e-12) {
                z.push_back(*it);
                zeros.erase(it);
            }
        }
        const auto b = quadratic(z), a = quadratic(p);
        out.push_back({b[0], b[1], b[2], a[0], a[1], a[2]});
    }
    if (out.empty()) out.push_back({1, 0, 0, 1, 0, 0});
    for (std::size_t i = 0; i < 3; ++i) out.front()[i] *= zpk.k;
    return out;
}

std::vector<C> zpk_response(const Zpk& zpk, std::span<const double> w) {
    std::vector<C> out;
    for (const double x : w) {
        const C e = std::exp(C{0, x});
        C h{zpk.k, 0};
        for (const auto& z : zpk.z) h *= e - z;
        for (const auto& p : zpk.p) h /= e - p;
        out.push_back(h);
    }
    return out;
}

std::vector<C> sos_response(std::span<const Sos> sos, std::span<const double> w) {
    std::vector<C> out;
    for (const double x : w) {
        const C e1 = std::exp(C{0, -x}), e2 = e1 * e1;
        C h{1, 0};
        for (const auto& s : sos) h *= (s[0] + s[1] * e1 + s[2] * e2) / (s[3] + s[4] * e1 + s[5] * e2);
        out.push_back(h);
    }
    return out;
}

FrequencyResponse zpk_frequency_response(const Zpk& zpk, std::span<const double> omega) {
    FrequencyResponse r;
    r.omega.assign(omega.begin(), omega.end());
    r.h = zpk_response(zpk, omega);
    const auto n = omega.size();
    r.magnitude.resize(n);
    r.magnitude_db.resize(n);
    r.phase.resize(n);
    r.group_delay.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        r.magnitude[k] = std::abs(r.h[k]);
        r.magnitude_db[k] = r.magnitude[k] > 0 ? std::max(-300.0, 20 * std::log10(r.magnitude[k])) : -300.0;
        r.phase[k] = std::arg(r.h[k]);
        const C e = std::exp(C{0, omega[k]});
        double tau = 0;
        for (const auto& z : zpk.z) tau -= (e / (e - z)).real();
        for (const auto& p : zpk.p) tau += (e / (e - p)).real();
        r.group_delay[k] = std::isfinite(tau) ? tau : std::numeric_limits<double>::quiet_NaN();
    }
    r.phase_unwrapped = unwrap_phase(r.phase);
    return r;
}

std::vector<double> sos_impulse(std::span<const Sos> sos, std::size_t n) {
    std::vector<double> x(n, 0.0);
    if (n) x[0] = 1;
    for (const auto& s : sos) {
        double z1 = 0, z2 = 0;
        for (auto& v : x) {
            const double y = s[0] / s[3] * v + z1;
            z1 = s[1] / s[3] * v - s[4] / s[3] * y + z2;
            z2 = s[2] / s[3] * v - s[5] / s[3] * y;
            v = y;
        }
    }
    return x;
}

bool is_stable(const Zpk& zpk) {
    return std::ranges::all_of(zpk.p, [](C p) { return std::abs(p) < 1; });
}

std::optional<IirFamily> parse_iir_family(std::string_view name) {
    if (name == "butter" || name == "butterworth") return IirFamily::butterworth;
    if (name == "cheby1" || name == "chebyshev1") return IirFamily::chebyshev1;
    if (name == "cheby2" || name == "chebyshev2") return IirFamily::chebyshev2;
    if (name == "ellip" || name == "elliptic") return IirFamily::elliptic;
    if (name == "bessel") return IirFamily::bessel;
    return std::nullopt;
}

std::optional<BandType> parse_band_type(std::string_view name) {
    if (name == "lowpass") return BandType::lowpass;
    if (name == "highpass") return BandType::highpass;
    if (name == "bandpass") return BandType::bandpass;
    if (name == "bandstop") return BandType::bandstop;
    return std::nullopt;
}

namespace {

Result<std::vector<std::complex<double>>> roots_in_z(std::vector<double> descending) {
    std::vector<std::complex<double>> out;
    while (descending.size() > 1 && descending.back() == 0.0) {
        descending.pop_back();
        out.emplace_back(0.0, 0.0);
    }
    if (descending.size() <= 1) return out;
    std::vector<double> ascending(descending.rbegin(), descending.rend());
    double norm = 0;
    for (double v : ascending) norm += std::abs(v);
    for (const double r : {-1.0, 1.0})
        while (ascending.size() > 1) {
            std::vector<double> q(ascending.size() - 1);
            double acc = ascending.back();
            for (std::size_t i = ascending.size() - 1; i-- > 0;) {
                q[i] = acc;
                acc = acc * r + ascending[i];
            }
            if (std::abs(acc) > 1e-10 * norm) break;
            ascending = std::move(q);
            out.emplace_back(r, 0.0);
        }
    if (ascending.size() <= 1) return out;
    auto roots = polynomial_roots<double>(ascending);
    if (!roots) return std::unexpected{roots.error()};
    for (const auto& r : *roots) out.emplace_back(r.re, std::abs(r.im) <= 1e-12 * std::max(1.0, std::abs(r.re)) ? 0.0 : r.im);
    return out;
}

}  // namespace

Result<Zpk> tf_to_zpk(const std::vector<double>& b, const std::vector<double>& a) {
    if (a.empty() || a.front() == 0.0) return make_error(Errc::invalid_argument, "tf_to_zpk: a[0] must not be zero");
    const auto first = std::find_if(b.begin(), b.end(), [](double v) { return v != 0.0; });
    if (first == b.end()) return make_error(Errc::invalid_argument, "tf_to_zpk: b has no nonzero coefficient");
    for (const auto* list : {&b, &a})
        for (double v : *list)
            if (!std::isfinite(v)) return make_error(Errc::invalid_argument, "tf_to_zpk: coefficients must be finite");
    const auto length = std::max(b.size(), a.size());
    std::vector<double> num(first, b.end()), den = a;
    num.resize(length - static_cast<std::size_t>(first - b.begin()), 0.0);
    den.resize(length, 0.0);
    auto z = roots_in_z(num);
    if (!z) return std::unexpected{z.error()};
    auto p = roots_in_z(den);
    if (!p) return std::unexpected{p.error()};
    return Zpk{std::move(*z), std::move(*p), *first / a.front()};
}

}  // namespace signals::core
