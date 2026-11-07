#include "core/cwt.hpp"

#include <algorithm>
#include <locale>
#include <cmath>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>

#include "core/detail/pocketfft.hpp"
#include "core/dwt.hpp"

namespace signals::core {

namespace {

constexpr int kPrecision = 12;
constexpr std::size_t kMaxLength = std::size_t{1} << 28;

struct Wavefun {
    std::vector<std::complex<double>> psi;
    std::vector<double> x;
};

Wavefun wavefun(const ContinuousWavelet& w, int precision) {
    const std::size_t p = std::size_t{1} << precision;
    Wavefun f;
    f.x.resize(p);
    const double step = (w.upper() - w.lower()) / static_cast<double>(p - 1);
    for (std::size_t i = 0; i < p; ++i) f.x[i] = static_cast<double>(i) * step + w.lower();
    f.x.back() = w.upper();
    f.psi.resize(p);
    for (std::size_t i = 0; i < p; ++i) {
        const double t = f.x[i];
        switch (w.family) {
        case ContinuousWavelet::Family::morl: f.psi[i] = std::cos(5 * t) * std::exp(-t * t / 2); break;
        case ContinuousWavelet::Family::mexh:
            f.psi[i] = 2 / (std::sqrt(3.0) * std::pow(std::numbers::pi, 0.25)) * (1 - t * t) * std::exp(-t * t / 2);
            break;
        case ContinuousWavelet::Family::cmor:
            f.psi[i] = std::exp(std::complex<double>{0, 2 * std::numbers::pi * w.center * t}) * std::exp(-t * t / w.bandwidth) /
                       std::sqrt(std::numbers::pi * w.bandwidth);
            break;
        }
    }
    return f;
}

std::optional<double> positive_number(std::string_view s) {
    if (s.empty()) return std::nullopt;
    std::istringstream in{std::string{s}};
    in.imbue(std::locale::classic());
    double v = 0;
    in >> v;
    if (in.fail() || in.peek() != std::char_traits<char>::eof() || !std::isfinite(v) || v <= 0) return std::nullopt;
    return v;
}

}  // namespace

Result<ContinuousWavelet> continuous_wavelet(std::string_view name) {
    if (name == "morl") return ContinuousWavelet{ContinuousWavelet::Family::morl};
    if (name == "mexh") return ContinuousWavelet{ContinuousWavelet::Family::mexh};
    if (name.starts_with("cmor")) {
        const auto params = name.substr(4);
        const auto dash = params.find('-');
        if (dash != std::string_view::npos) {
            const auto b = positive_number(params.substr(0, dash));
            const auto c = positive_number(params.substr(dash + 1));
            if (b && c) return ContinuousWavelet{ContinuousWavelet::Family::cmor, *b, *c};
        }
    }
    return make_error(Errc::unknown_wavelet, "cwt: unknown continuous wavelet " + std::string{name} + " (morl, mexh, cmorB-C)");
}

double center_frequency(const ContinuousWavelet& w, int precision) {
    auto f = wavefun(w, precision);
    const auto n = f.psi.size();
    const pocketfft::shape_t shape{n};
    const pocketfft::stride_t stride{sizeof(std::complex<double>)};
    std::vector<std::complex<double>> spectrum(n);
    pocketfft::c2c(shape, stride, stride, {0}, pocketfft::FORWARD, f.psi.data(), spectrum.data(), 1.0);
    std::size_t best = 1;
    for (std::size_t k = 2; k < n; ++k)
        if (std::abs(spectrum[k]) > std::abs(spectrum[best])) best = k;
    auto index = static_cast<double>(best + 1);
    if (index > static_cast<double>(n) / 2) index = static_cast<double>(n) - index + 2;
    const double domain = f.x.back() - f.x.front();
    return 1.0 / (domain / (index - 1));
}

Result<void> cwt_rows(std::span<const double> x, std::span<const double> scales, const ContinuousWavelet& w, const CwtRow& out,
                      ProgressSink* progress) {
    if (x.empty()) return make_error(Errc::invalid_argument, "cwt: empty signal");
    const auto f = wavefun(w, kPrecision);
    const double step = f.x[1] - f.x[0];
    std::vector<std::complex<double>> int_psi(f.psi.size());
    std::complex<double> run{0, 0};
    for (std::size_t i = 0; i < f.psi.size(); ++i) {
        run += f.psi[i];
        int_psi[i] = w.complex() ? std::conj(run * step) : run * step;
    }
    const auto n = x.size();
    std::vector<std::vector<std::size_t>> picks(scales.size());
    std::size_t longest = 0;
    for (std::size_t i = 0; i < scales.size(); ++i) {
        const double s = scales[i];
        if (!std::isfinite(s) || s <= 0) return make_error(Errc::invalid_argument, "cwt: scales must be positive");
        const double count = std::ceil(s * (f.x.back() - f.x.front()) + 1);
        if (count > static_cast<double>(kMaxLength)) return make_error(Errc::invalid_argument, "cwt: scale too large");
        const double denom = s * step;
        for (std::size_t k = 0; k < static_cast<std::size_t>(count); ++k) {
            const auto j = static_cast<std::size_t>(static_cast<double>(k) / denom);
            if (j < int_psi.size()) picks[i].push_back(j);
        }
        if (picks[i].size() < 2) return make_error(Errc::invalid_argument, "cwt: scale " + std::to_string(s) + " too small");
        longest = std::max(longest, picks[i].size());
    }
    if (n + longest - 1 > kMaxLength) return make_error(Errc::invalid_argument, "cwt: signal and wavelet too long");
    const std::size_t m = pocketfft::detail::util::good_size_cmplx(n + longest - 1);
    const pocketfft::shape_t shape{m};
    const pocketfft::stride_t stride{sizeof(std::complex<double>)};
    std::vector<std::complex<double>> spectrum(m), kernel(m), conv(m), row(n);
    for (std::size_t k = 0; k < n; ++k) conv[k] = x[k];
    pocketfft::c2c(shape, stride, stride, {0}, pocketfft::FORWARD, conv.data(), spectrum.data(), 1.0);
    for (std::size_t i = 0; i < scales.size(); ++i) {
        if (progress && progress->cancelled()) return std::unexpected{cancelled_error("cwt")};
        const auto& j = picks[i];
        const auto len = j.size();
        std::fill(conv.begin(), conv.end(), std::complex<double>{});
        for (std::size_t k = 0; k < len; ++k) conv[k] = int_psi[j[len - 1 - k]];
        pocketfft::c2c(shape, stride, stride, {0}, pocketfft::FORWARD, conv.data(), kernel.data(), 1.0);
        for (std::size_t k = 0; k < m; ++k) kernel[k] *= spectrum[k];
        pocketfft::c2c(shape, stride, stride, {0}, pocketfft::BACKWARD, kernel.data(), conv.data(), 1.0 / static_cast<double>(m));
        const auto offset = (len - 2) / 2;
        const double gain = -std::sqrt(scales[i]);
        for (std::size_t t = 0; t < n; ++t) {
            const auto v = gain * (conv[t + offset + 1] - conv[t + offset]);
            row[t] = w.complex() ? v : std::complex<double>{v.real(), 0};
        }
        out(i, row);
        if (progress) progress->report(static_cast<double>(i + 1) / static_cast<double>(scales.size()));
    }
    return {};
}

Result<std::vector<std::vector<std::complex<double>>>> cwt(std::span<const double> x, std::span<const double> scales,
                                                           const ContinuousWavelet& w, ProgressSink* progress) {
    std::vector<std::vector<std::complex<double>>> out(scales.size());
    auto ok = cwt_rows(x, scales, w, [&](std::size_t i, std::span<const std::complex<double>> row) { out[i].assign(row.begin(), row.end()); },
                       progress);
    if (!ok) return std::unexpected{ok.error()};
    return out;
}

}  // namespace signals::core
