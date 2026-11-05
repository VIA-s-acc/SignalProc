#include "support/banks.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "core/dwt.hpp"
#include "core/metrics.hpp"
#include "support/golden.hpp"

namespace signals::test {

const nlohmann::json& golden_wavelet(std::string_view name) {
    static const nlohmann::json filters = load_golden("filters.json");
    for (const auto& w : filters["wavelets"])
        if (w["name"] == name) return w;
    throw std::runtime_error("wavelet not in golden: " + std::string{name});
}

core::FilterBank golden_bank(std::string_view name) {
    const auto& w = golden_wavelet(name);
    return core::bank_from_pywt<double>(std::string{name}, w["dec_lo"], w["dec_hi"], w["rec_lo"], w["rec_hi"]);
}

std::vector<double> signal_of_length(std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) {
        const auto t = static_cast<double>(k);
        x[k] = std::sin(0.3 * t) + 0.5 * std::cos(1.7 * t) + (static_cast<double>(k % 5) - 2.0) / 4.0;
    }
    return x;
}

double max_abs(std::span<const double> x) {
    double m = 0.0;
    for (double v : x) m = std::max(m, std::abs(v));
    return m;
}

double round_trip_error(const std::vector<double>& x, std::uint8_t wavelet_index, std::uint8_t mode_index) {
    const auto bank = golden_bank(pr_representative[wavelet_index % pr_representative.size()]);
    const core::DwtOptions options{all_modes[mode_index % all_modes.size()], false};
    auto level = core::dwt_level<double>(x, bank, options);
    if (!level) return INFINITY;
    auto y = core::idwt_level<double>(level->approx, level->detail, bank, options);
    if (!y || y->size() < x.size()) return INFINITY;
    y->resize(x.size());
    return core::max_abs_error(*y, x);
}

}  // namespace signals::test
