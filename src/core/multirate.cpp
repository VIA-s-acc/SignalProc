#include "core/multirate.hpp"

#include <algorithm>
#include <limits>

#include "core/conv.hpp"
#include "core/design/window_fir.hpp"

namespace signals::core {
namespace {

constexpr std::size_t stuffed_limit = std::numeric_limits<std::ptrdiff_t>::max() / sizeof(double);

Result<void> require_factor(std::size_t m) {
    if (m == 0) return make_error(Errc::invalid_argument, "resampling factor must be at least 1");
    return {};
}

std::size_t filter_delay(const Laurent& h, const MultirateOptions& options) {
    return options.compensate_delay ? (h.coeffs.size() - 1) / 2 : 0;
}

}  // namespace

std::vector<double> upfirdn(const Laurent& h, std::span<const double> x, std::size_t up, std::size_t down) {
    const std::size_t n = x.size(), taps = h.coeffs.size();
    if (n == 0 || taps == 0 || up == 0 || down == 0) return {};
    const std::size_t full = (n - 1) * up + taps;
    std::vector<double> y((full - 1) / down + 1, 0.0);
    for (std::size_t i = 0; i < y.size(); ++i) {
        const std::size_t t = i * down;
        const std::size_t k_first = t + 1 > taps ? (t + 1 - taps + up - 1) / up : 0;
        const std::size_t k_last = std::min(n - 1, t / up);
        double acc = 0;
        for (std::size_t k = k_first; k <= k_last; ++k) acc += x[k] * h.coeffs[t - k * up];
        y[i] = acc;
    }
    return y;
}

Laurent anti_alias_filter(std::size_t m, const MultirateOptions& options) {
    const double width = options.transition / static_cast<double>(m);
    const auto order = kaiser_order(options.attenuation_db, width);
    return kaiser_lowpass(order.taps | 1, 1.0 / static_cast<double>(m), order.beta);
}

Result<std::vector<double>> decimate(std::span<const double> x, std::size_t m, const MultirateOptions& options) {
    if (auto ok = require_factor(m); !ok) return std::unexpected{ok.error()};
    const std::size_t out_len = (x.size() + m - 1) / m;
    std::vector<double> y(out_len);
    if (m == 1) return std::vector<double>(x.begin(), x.end());
    if (!options.anti_alias) {
        for (std::size_t k = 0; k < out_len; ++k) y[k] = x[k * m];
        return y;
    }
    const auto h = anti_alias_filter(m, options);
    const auto filtered = convolve(Laurent{{x.begin(), x.end()}, 0}, h);
    const std::size_t d = filter_delay(h, options);
    for (std::size_t k = 0; k < out_len; ++k) y[k] = filtered.coeffs[k * m + d];
    return y;
}

Result<std::vector<double>> interpolate(std::span<const double> x, std::size_t m, const MultirateOptions& options) {
    if (auto ok = require_factor(m); !ok) return std::unexpected{ok.error()};
    if (!x.empty() && m > stuffed_limit / x.size()) return make_error(Errc::invalid_argument, "interpolated length overflows");
    std::vector<double> stuffed(x.size() * m, 0.0);
    for (std::size_t k = 0; k < x.size(); ++k) stuffed[k * m] = x[k];
    if (m == 1 || !options.anti_alias || x.empty()) return stuffed;
    auto h = anti_alias_filter(m, options);
    for (double& c : h.coeffs) c *= static_cast<double>(m);
    const auto filtered = convolve(Laurent{std::move(stuffed), 0}, h);
    const std::size_t d = filter_delay(h, options);
    return std::vector<double>(filtered.coeffs.begin() + static_cast<std::ptrdiff_t>(d),
                               filtered.coeffs.begin() + static_cast<std::ptrdiff_t>(d + x.size() * m));
}

}  // namespace signals::core
