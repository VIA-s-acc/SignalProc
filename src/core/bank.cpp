#include "core/bank.hpp"

#include <algorithm>
#include <cmath>

namespace signals::core {
namespace {

double nan_max(double a, double b) { return std::isnan(a) || std::isnan(b) ? std::nan("") : std::max(a, b); }

double max_abs(const Laurent& p) {
    double m = 0.0;
    for (double c : p.coeffs) m = nan_max(m, std::abs(c));
    return m;
}

}  // namespace

PrReport check_pr(const FilterBank& bank, double tol) {
    const auto alias = scale(add(multiply(bank.f0, alternate(bank.h0)), multiply(bank.f1, alternate(bank.h1))), 0.5);
    const auto distortion = scale(add(multiply(bank.f0, bank.h0), multiply(bank.f1, bank.h1)), 0.5);
    PrReport report{{"check.pr_alias", bank.name, max_abs(alias), tol}, {"check.pr_distortion", bank.name, 0.0, tol}, {}};
    if (distortion.coeffs.empty()) {
        report.distortion.value = 1.0;
        return report;
    }
    const auto peak = std::ranges::max_element(distortion.coeffs, {}, [](double c) { return std::abs(c); });
    const auto ell = distortion.offset + (peak - distortion.coeffs.begin());
    double deviation = 0.0;
    for (std::size_t k = 0; k < distortion.coeffs.size(); ++k) {
        const auto n = distortion.offset + static_cast<std::ptrdiff_t>(k);
        deviation = nan_max(deviation, std::abs(distortion.coeffs[k] - (n == ell ? 1.0 : 0.0)));
    }
    report.distortion.value = deviation;
    if (report.distortion.passed() && report.alias.passed()) report.delay = ell;
    return report;
}

CheckResult check_orthogonality(const FilterBank& bank, double tol) {
    const auto& h = bank.h0.coeffs;
    const auto& f = bank.f0.coeffs;
    const auto n = static_cast<std::ptrdiff_t>(h.size());
    double worst = 0.0;
    for (std::ptrdiff_t k = 0; 2 * k < n; ++k) {
        double s = 0.0;
        for (std::ptrdiff_t i = 0; i + 2 * k < n; ++i) s += h[static_cast<std::size_t>(i)] * h[static_cast<std::size_t>(i + 2 * k)];
        worst = nan_max(worst, std::abs(s - (k == 0 ? 1.0 : 0.0)));
    }
    if (f.size() != h.size()) {
        worst = std::max(worst, 1.0);
    } else {
        for (std::size_t i = 0; i < f.size(); ++i) worst = nan_max(worst, std::abs(f[i] - h[h.size() - 1 - i]));
    }
    return {"check.orthogonality", bank.name, worst, tol};
}

bool is_linear_phase(const Laurent& h, double tol) {
    const auto t = trim(h, tol);
    const auto n = t.coeffs.size();
    bool symmetric = true, antisymmetric = true;
    for (std::size_t i = 0; i < n; ++i) {
        symmetric = symmetric && std::abs(t.coeffs[i] - t.coeffs[n - 1 - i]) <= tol;
        antisymmetric = antisymmetric && std::abs(t.coeffs[i] + t.coeffs[n - 1 - i]) <= tol;
    }
    return n > 0 && (symmetric || antisymmetric);
}

}  // namespace signals::core
