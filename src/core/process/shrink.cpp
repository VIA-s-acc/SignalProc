#include "core/process/shrink.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/wavedec.hpp"

namespace signals::core {
namespace {

double median(std::vector<double> v) {
    const auto mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    const double upper = v[mid];
    if (v.size() % 2 == 1) return upper;
    const double lower = *std::max_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid));
    return (lower + upper) / 2;
}

std::size_t default_levels(std::size_t n, std::size_t filter_len) {
    const auto max_level = dwt_max_level(n, filter_len);
    return max_level > 4 ? max_level - 3 : 1;
}

}  // namespace

double noise_sigma_mad(std::span<const double> finest_detail) {
    std::vector<double> magnitudes;
    magnitudes.reserve(finest_detail.size());
    for (double d : finest_detail)
        if (d != 0) magnitudes.push_back(std::abs(d));
    if (magnitudes.empty()) return 0.0;
    return median(std::move(magnitudes)) / kNormalQuartile;
}

double universal_threshold(double sigma, std::size_t n) {
    return sigma * std::sqrt(2 * std::log(static_cast<double>(n)));
}

double bayes_threshold(std::span<const double> detail, double sigma) {
    if (sigma == 0 || detail.empty()) return 0.0;
    const double var = sigma * sigma;
    const double mean_square = energy(detail) / static_cast<double>(detail.size());
    return var / std::sqrt(std::max(mean_square - var, DBL_EPSILON));
}

Result<std::pair<std::vector<double>, DenoiseReport>> denoise(std::span<const double> x, const DenoiseOptions& options) {
    const auto bank = wavelet(options.wavelet);
    if (!bank) return std::unexpected{bank.error()};
    const auto levels = options.levels.value_or(default_levels(x.size(), bank->length()));
    const DwtOptions dwt{options.mode, false};
    auto coeffs = wavedec<double>(x, *bank, levels, dwt);
    if (!coeffs) return std::unexpected{coeffs.error()};

    DenoiseReport report{options.sigma.value_or(noise_sigma_mad(coeffs->bands.back())), levels, {}};
    for (std::size_t b = 1; b < coeffs->bands.size(); ++b) {
        const double t = options.rule == ShrinkRule::bayes ? bayes_threshold(coeffs->bands[b], report.sigma)
                                                           : universal_threshold(report.sigma, x.size());
        report.thresholds.push_back(t);
    }
    if (report.sigma == 0) return std::pair{std::vector<double>(x.begin(), x.end()), std::move(report)};

    for (std::size_t b = 1; b < coeffs->bands.size(); ++b)
        coeffs->bands[b] = threshold(coeffs->bands[b], report.thresholds[b - 1], options.threshold);
    auto y = waverec<double>(*coeffs, *bank, dwt);
    if (!y) return std::unexpected{y.error()};
    y->resize(x.size());
    return std::pair{std::move(*y), std::move(report)};
}

Result<std::pair<AudioBuffer, std::vector<DenoiseReport>>> denoise(const AudioBuffer& audio, const DenoiseOptions& options) {
    std::pair<AudioBuffer, std::vector<DenoiseReport>> out{AudioBuffer{audio.sample_rate, {}}, {}};
    for (const auto& channel : audio.channels) {
        auto r = denoise(channel, options);
        if (!r) return std::unexpected{r.error()};
        out.first.channels.push_back(std::move(r->first));
        out.second.push_back(std::move(r->second));
    }
    return out;
}

Result<std::vector<double>> edit_bands(std::span<const double> x, const FilterBank& bank, std::size_t levels,
                                       const DwtOptions& options, std::span<const double> gains) {
    if (gains.size() != levels + 1)
        return make_error(Errc::invalid_argument, "edit_bands: need " + std::to_string(levels + 1) + " gains, got " +
                                                      std::to_string(gains.size()));
    auto coeffs = wavedec<double>(x, bank, levels, options);
    if (!coeffs) return std::unexpected{coeffs.error()};
    for (std::size_t b = 0; b < coeffs->bands.size(); ++b)
        for (double& c : coeffs->bands[b]) c *= gains[b];
    return waverec<double>(*coeffs, bank, options);
}

}  // namespace signals::core
