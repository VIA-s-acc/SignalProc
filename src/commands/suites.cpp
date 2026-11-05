#include "commands/suites.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/design/families.hpp"
#include "core/matrix.hpp"
#include "core/metrics.hpp"
#include "core/wavedec.hpp"

namespace signals::cli {
namespace {

std::vector<double> test_signal(std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) {
        const auto t = static_cast<double>(k);
        x[k] = std::sin(0.3 * t) + 0.5 * std::cos(1.7 * t) + (static_cast<double>(k % 5) - 2.0) / 4.0;
    }
    return x;
}

double peak(const std::vector<double>& x) {
    double m = 0.0;
    for (double v : x) m = std::max(m, std::abs(v));
    return m;
}

}  // namespace

double base_tolerance(bool generated, bool approximate) {
    if (approximate) return 5e-3;
    return generated ? 1e-12 : 1e-10;
}

std::vector<core::CheckResult> core_suite() {
    using namespace signals::core;
    std::vector<CheckResult> results;
    const auto x = test_signal(1000);
    const double x_peak = peak(x);
    // Parseval needs every level length even under periodization, hence a power of two.
    const auto x_pow2 = test_signal(1024);
    const double x_pow2_energy = energy(x_pow2);
    for (const auto name : wavelet_names()) {
        const auto bank = wavelet(name);
        const auto info = wavelet_info(name);
        const std::string subject{name};
        if (!bank || !info) {
            results.push_back({"check.wavelet_available", subject, INFINITY, 0.0});
            continue;
        }
        const double tol = base_tolerance(info->generated, name == "dmey");
        const auto pr = check_pr(*bank, tol);
        results.push_back(pr.alias);
        results.push_back(pr.distortion);
        if (info->orthogonal) results.push_back(check_orthogonality(*bank, tol));
        const auto levels = dwt_max_level(x.size(), bank->length());
        for (const auto mode : {Extension::symmetric, Extension::periodization}) {
            const DwtOptions options{mode, false};
            const std::string where = subject + "/" + std::string{to_string(mode)};
            const auto coeffs = wavedec<double>(x, *bank, levels, options);
            const auto y = coeffs ? waverec<double>(*coeffs, *bank, options) : Result<std::vector<double>>{};
            const double round_trip_tol =
                default_tolerance(levels, bank->length(), x_peak) + tol * static_cast<double>(levels) * (1.0 + x_peak);
            results.push_back({"check.round_trip", where, y ? max_abs_error(*y, x) : INFINITY, round_trip_tol});
            if (info->orthogonal && mode == Extension::periodization) {
                const auto pow2_levels = dwt_max_level(x_pow2.size(), bank->length());
                const auto c = wavedec<double>(x_pow2, *bank, pow2_levels, options);
                double e = 0.0;
                if (c)
                    for (const auto& band : c->bands) e += energy(band);
                results.push_back({"check.parseval", where, c ? std::abs(e - x_pow2_energy) / x_pow2_energy : INFINITY,
                                   std::max(1e-13, tol * static_cast<double>(pow2_levels))});
            }
        }
    }
    const auto db2 = wavelet("db2");
    for (const auto mode : {Extension::zero, Extension::constant, Extension::symmetric, Extension::reflect,
                            Extension::periodic, Extension::smooth, Extension::periodization, Extension::antisymmetric,
                            Extension::antireflect}) {
        const auto s = db2 ? synthesis_matrix(*db2, 32, {mode}) : Result<Eigen::MatrixXd>{};
        const auto a = db2 ? analysis_matrix(*db2, 32, {mode}) : Result<Eigen::MatrixXd>{};
        auto r = s && a ? check_left_inverse(*s, *a, 1e-13) : CheckResult{"check.sa_identity", "", INFINITY, 1e-13};
        r.subject = "db2/" + std::string{to_string(mode)};
        results.push_back(r);
    }
    return results;
}

}  // namespace signals::cli
