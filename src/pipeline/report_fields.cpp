#include "pipeline/report_fields.hpp"

#include <array>
#include <cctype>
#include <utility>

namespace signals::pipeline {
namespace {

using Fields = std::span<const std::string_view>;

constexpr std::string_view kAssert[]{"value", "*"};
constexpr std::string_view kBandEnergy[]{"c{c}.{band}"};
constexpr std::string_view kBandPower[]{"{band}_ch{c}"};
constexpr std::string_view kCheck[]{"max_abs", "rms", "snr_db"};
constexpr std::string_view kCoeffStats[]{"c{c}.{band}.energy", "c{c}.{band}.entropy", "c{c}.{band}.sparsity"};
constexpr std::string_view kStats[]{"mean_ch{c}", "std_ch{c}", "rms_ch{c}", "peak_ch{c}", "crest_db_ch{c}", "zcr_per_s_ch{c}", "centroid_hz_ch{c}", "dominant_hz_ch{c}"};
constexpr std::string_view kCorrelation[]{"pearson_ch{c}", "spearman_ch{c}", "cosine_ch{c}", "n_ch{c}", "mean_ch{c}", "std_ch{c}", "rms_ch{c}", "peak_ch{c}",
                                          "crest_db_ch{c}", "zcr_per_s_ch{c}", "centroid_hz_ch{c}", "dominant_hz_ch{c}"};
constexpr std::string_view kDelay[]{"lag_samples_ch{c}", "lag_s_ch{c}", "peak_lag_ch{c}", "peak_corr_ch{c}"};
constexpr std::string_view kExport[]{"path", "bytes"};
constexpr std::string_view kHistogram[]{"counts_ch{c}", "lo_ch{c}", "hi_ch{c}", "in_range_ch{c}", "outside_ch{c}", "mode_ch{c}"};
constexpr std::string_view kLoudness[]{"max_dbfs", "mean_dbfs", "frames"};
constexpr std::string_view kLpc[]{"order", "a{k}_ch{c}", "k{k}_ch{c}", "gain_db_ch{c}"};
constexpr std::string_view kMfcc[]{"mfcc{k}_ch{c}", "frames_ch{c}"};
constexpr std::string_view kOnsets[]{"count", "times_s"};
constexpr std::string_view kPitch[]{"f0_median_hz_ch{c}", "voiced_fraction_ch{c}", "frames_ch{c}"};
constexpr std::string_view kSpectral[]{"centroid_hz_ch{c}", "spread_hz_ch{c}", "flatness_ch{c}", "rolloff_hz_ch{c}", "flux_ch{c}", "frames_ch{c}"};
constexpr std::string_view kPeaks[]{"peaks_ch{c}", "peak{k}_hz_ch{c}", "peak{k}_db_ch{c}"};
constexpr std::string_view kRidge[]{"freq_min_hz",        "freq_median_hz",        "freq_max_hz",        "amp_peak",        "found_fraction",
                                    "freq_min_hz_ch{c}", "freq_median_hz_ch{c}", "freq_max_hz_ch{c}", "amp_peak_ch{c}", "found_fraction_ch{c}"};
constexpr std::string_view kThd[]{"f0_hz_ch{c}", "thd_db_ch{c}", "thd_percent_ch{c}"};
constexpr std::string_view kWavOut[]{"path", "clipped"};

constexpr std::array<std::pair<std::string_view, Fields>, 21> kTable{{
    {"assert", kAssert},
    {"band_energy", kBandEnergy},
    {"band_power", kBandPower},
    {"check", kCheck},
    {"coeff_stats", kCoeffStats},
    {"correlation", kCorrelation},
    {"delay_estimate", kDelay},
    {"export", kExport},
    {"export_report", kExport},
    {"histogram", kHistogram},
    {"loudness", kLoudness},
    {"lpc", kLpc},
    {"mfcc", kMfcc},
    {"onsets", kOnsets},
    {"pitch", kPitch},
    {"spectral_features", kSpectral},
    {"spectrum_peaks", kPeaks},
    {"sst_ridge", kRidge},
    {"stats", kStats},
    {"thd", kThd},
    {"wav_out", kWavOut},
}};

bool digits(std::string_view s, std::size_t& at) {
    const auto start = at;
    while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) ++at;
    return at > start;
}

bool match(std::string_view p, std::string_view f) {
    if (p.empty()) return f.empty();
    if (p == "*") return true;
    for (const std::string_view hole : {"{c}", "{k}"})
        if (p.starts_with(hole)) {
            std::size_t at = 0;
            return digits(f, at) && match(p.substr(hole.size()), f.substr(at));
        }
    if (p.starts_with("{band}")) {
        for (std::size_t n = 1; n <= f.size(); ++n) {
            const char c = f[n - 1];
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_') break;
            if (match(p.substr(6), f.substr(n))) return true;
        }
        return false;
    }
    return !f.empty() && p.front() == f.front() && match(p.substr(1), f.substr(1));
}

}  // namespace

std::span<const std::string_view> report_fields(std::string_view block) {
    for (const auto& [id, fields] : kTable)
        if (id == block) return fields;
    return {};
}

bool field_matches(std::string_view pattern, std::string_view field) { return match(pattern, field); }

}  // namespace signals::pipeline
