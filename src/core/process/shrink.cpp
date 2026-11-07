#include "core/process/shrink.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

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

}  // namespace

std::size_t default_levels(std::size_t n, std::size_t filter_len) {
    const auto max_level = dwt_max_level(n, filter_len);
    return max_level > 4 ? max_level - 3 : 1;
}

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

double sure_threshold(std::span<const double> detail, double sigma) {
    const auto n = detail.size();
    if (n == 0 || !(sigma > 0)) return 0.0;
    std::vector<double> sx2(n);
    double norm2 = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double v = detail[i] / sigma;
        sx2[i] = v * v;
        norm2 += v * v;
    }
    const double universal = std::sqrt(2 * std::log(static_cast<double>(n)));
    const double nd = static_cast<double>(n);
    if ((norm2 - nd) / nd < std::pow(std::log2(nd), 1.5) / std::sqrt(nd)) return sigma * universal;
    std::ranges::sort(sx2);
    double cumulative = 0, best_risk = INFINITY, best = 0;
    for (std::size_t i = 0; i < n; ++i) {
        cumulative += sx2[i];
        const double risk = (nd - 2 * static_cast<double>(i + 1) + cumulative + static_cast<double>(n - 1 - i) * sx2[i]) / nd;
        if (risk < best_risk) best_risk = risk, best = sx2[i];
    }
    return sigma * std::min(std::sqrt(best), universal);
}

double minimax_threshold(double sigma, std::size_t n) {
    if (n <= 32 || !(sigma > 0)) return 0.0;
    return sigma * (0.3936 + 0.1829 * std::log2(static_cast<double>(n)));
}

double rule_threshold(ShrinkRule rule, std::span<const double> detail, double sigma, std::size_t n) {
    switch (rule) {
    case ShrinkRule::bayes: return bayes_threshold(detail, sigma);
    case ShrinkRule::visu: return universal_threshold(sigma, n);
    case ShrinkRule::sure: return sure_threshold(detail, sigma);
    case ShrinkRule::minimax: return minimax_threshold(sigma, n);
    }
    return 0.0;
}

std::optional<ShrinkRule> parse_shrink_rule(std::string_view name) {
    if (name == "bayes") return ShrinkRule::bayes;
    if (name == "visu") return ShrinkRule::visu;
    if (name == "sure") return ShrinkRule::sure;
    if (name == "minimax") return ShrinkRule::minimax;
    return std::nullopt;
}

std::string_view shrink_rule_name(ShrinkRule rule) {
    switch (rule) {
    case ShrinkRule::bayes: return "BayesShrink";
    case ShrinkRule::visu: return "VisuShrink";
    case ShrinkRule::sure: return "SureShrink";
    case ShrinkRule::minimax: return "Minimax";
    }
    return "?";
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
        const double t = rule_threshold(options.rule, coeffs->bands[b], report.sigma, x.size());
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

std::size_t keep_largest(std::vector<std::vector<double>>& bands, double fraction, std::size_t first_band) {
    std::vector<std::pair<std::size_t, std::size_t>> where;
    for (std::size_t b = first_band; b < bands.size(); ++b)
        for (std::size_t i = 0; i < bands[b].size(); ++i) where.emplace_back(b, i);
    const auto total = where.size();
    const auto keep = std::min(total, static_cast<std::size_t>(std::ceil(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(total))));
    std::stable_sort(where.begin(), where.end(), [&](const auto& x, const auto& y) {
        return std::abs(bands[x.first][x.second]) > std::abs(bands[y.first][y.second]);
    });
    for (std::size_t k = keep; k < total; ++k) bands[where[k].first][where[k].second] = 0.0;
    return keep;
}

Result<std::vector<double>> cycle_spin(std::span<const double> x, std::size_t shifts, const SignalMap& f) {
    if (shifts == 0) return make_error(Errc::invalid_argument, "cycle_spin: at least one shift is needed");
    const auto n = x.size();
    std::vector<double> sum(n, 0.0), rolled(n);
    for (std::size_t s = 0; s < shifts; ++s) {
        for (std::size_t i = 0; i < n; ++i) rolled[(i + s) % n] = x[i];
        auto y = f(rolled);
        if (!y) return std::unexpected{y.error()};
        if (y->size() != n) return make_error(Errc::length_mismatch, "cycle_spin: the map changed the length");
        for (std::size_t i = 0; i < n; ++i) sum[i] += (*y)[(i + s) % n];
    }
    for (auto& v : sum) v /= static_cast<double>(shifts);
    return sum;
}

}  // namespace signals::core
