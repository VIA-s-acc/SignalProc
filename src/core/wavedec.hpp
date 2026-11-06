#pragma once

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "core/dwt.hpp"

namespace signals::core {

template <class T>
struct BasicCoeffs {
    std::vector<std::vector<T>> bands;
    std::size_t original_length = 0;
};

std::size_t dwt_max_level(std::size_t n, std::size_t filter_len);

template <class T>
Result<BasicCoeffs<T>> wavedec(std::span<const T> x, const BasicFilterBank<T>& bank, std::size_t levels,
                               const DwtOptions& options, ProgressSink* progress = nullptr) {
    const auto max_level = dwt_max_level(x.size(), bank.length());
    if (max_level == 0)
        return make_error(Errc::level_out_of_range, "signal too short: n=" + std::to_string(x.size()) +
                                                        " needs at least L-1=" + std::to_string(bank.length() - 1) +
                                                        " samples");
    if (levels < 1 || levels > max_level)
        return make_error(Errc::level_out_of_range, "wavedec: levels must be in [1, " + std::to_string(max_level) +
                                                        "] for n=" + std::to_string(x.size()) + ", L=" +
                                                        std::to_string(bank.length()));
    BasicCoeffs<T> out;
    out.original_length = x.size();
    std::vector<std::vector<T>> details;
    std::vector<T> approx(x.begin(), x.end());
    for (std::size_t j = 0; j < levels; ++j) {
        auto level = dwt_level<T>(approx, bank, options, progress);
        if (!level) return std::unexpected(level.error());
        details.push_back(std::move(level->detail));
        approx = std::move(level->approx);
        if (progress) progress->report(j + 1 == levels ? 1.0 : 1.0 - std::ldexp(1.0, -static_cast<int>(j + 1)));
    }
    out.bands.push_back(std::move(approx));
    for (auto it = details.rbegin(); it != details.rend(); ++it) out.bands.push_back(std::move(*it));
    return out;
}

template <class T>
Result<std::vector<T>> waverec(const BasicCoeffs<T>& coeffs, const BasicFilterBank<T>& bank,
                               const DwtOptions& options, ProgressSink* progress = nullptr) {
    if (coeffs.bands.size() < 2) return make_error(Errc::invalid_argument, "waverec: need approximation and details");
    std::vector<T> approx = coeffs.bands.front();
    for (std::size_t k = 1; k < coeffs.bands.size(); ++k) {
        const auto& detail = coeffs.bands[k];
        if (approx.size() == detail.size() + 1) approx.pop_back();
        if (approx.size() != detail.size())
            return make_error(Errc::length_mismatch, "waverec: band " + std::to_string(k) + " has " +
                                                         std::to_string(detail.size()) + " coefficients, expected " +
                                                         std::to_string(approx.size()));
        auto rec = idwt_level<T>(approx, detail, bank, options, progress);
        if (!rec) return std::unexpected(rec.error());
        approx = std::move(*rec);
        const auto levels = static_cast<int>(coeffs.bands.size() - 1);
        if (progress) progress->report((std::ldexp(1.0, static_cast<int>(k)) - 1) / (std::ldexp(1.0, levels) - 1));
    }
    if (coeffs.original_length != 0) {
        if (approx.size() < coeffs.original_length)
            return make_error(Errc::length_mismatch, "waverec: reconstruction shorter than the original signal");
        approx.resize(coeffs.original_length);
    }
    return approx;
}

}  // namespace signals::core
