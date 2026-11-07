#include "core/tempo.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <map>
#include <numbers>

#include "core/analysis.hpp"
#include "core/detail/pocketfft.hpp"

namespace signals::core {
namespace {

using C = std::complex<double>;

std::vector<double> periodic_hann(std::size_t n) {
    std::vector<double> w(n);
    for (std::size_t k = 0; k < n; ++k) w[k] = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n));
    return w;
}

std::size_t output_length(std::size_t n, double rate) { return static_cast<std::size_t>(std::nearbyint(static_cast<double>(n) / rate)); }

struct Spectrum {
    std::vector<double> mag, phase;
};

class CenteredFrames {
public:
    CenteredFrames(std::span<const double> x, std::size_t n, std::size_t hop) : x_{x}, n_{n}, hop_{hop}, window_{periodic_hann(n)}, buffer_(n), bins_(n / 2 + 1) {}

    std::size_t count() const { return 1 + x_.size() / hop_; }

    const Spectrum& at(std::size_t k) {
        if (const auto it = cache_.find(k); it != cache_.end()) return it->second;
        const auto half = n_ / 2;
        for (std::size_t j = 0; j < n_; ++j) {
            const auto p = k * hop_ + j;
            buffer_[j] = p >= half && p - half < x_.size() ? x_[p - half] * window_[j] : 0.0;
        }
        pocketfft::r2c({n_}, {sizeof(double)}, {sizeof(C)}, 0, pocketfft::FORWARD, buffer_.data(), bins_.data(), 1.0);
        Spectrum s{std::vector<double>(bins_.size()), std::vector<double>(bins_.size())};
        for (std::size_t b = 0; b < bins_.size(); ++b) {
            s.mag[b] = std::abs(bins_[b]);
            s.phase[b] = std::arg(bins_[b]);
        }
        return cache_.emplace(k, std::move(s)).first->second;
    }

    void forget_before(std::size_t k) { cache_.erase(cache_.begin(), cache_.lower_bound(k)); }

private:
    std::span<const double> x_;
    std::size_t n_, hop_;
    std::vector<double> window_, buffer_;
    std::vector<C> bins_;
    std::map<std::size_t, Spectrum> cache_;
};

class OverlapAdd {
public:
    OverlapAdd(std::size_t length, std::size_t n, std::size_t hop) : y_(length, 0.0), norm_(length, 0.0), window_{periodic_hann(n)}, frame_(n), n_{n}, hop_{hop} {}

    void add(std::size_t j, std::vector<C>& spectrum) {
        pocketfft::c2r({n_}, {sizeof(C)}, {sizeof(double)}, 0, pocketfft::BACKWARD, spectrum.data(), frame_.data(), 1.0 / static_cast<double>(n_));
        const auto half = static_cast<std::ptrdiff_t>(n_ / 2);
        for (std::size_t k = 0; k < n_; ++k) {
            const auto pos = static_cast<std::ptrdiff_t>(j * hop_ + k) - half;
            if (pos < 0 || pos >= static_cast<std::ptrdiff_t>(y_.size())) continue;
            y_[static_cast<std::size_t>(pos)] += window_[k] * frame_[k];
            norm_[static_cast<std::size_t>(pos)] += window_[k] * window_[k];
        }
    }

    std::vector<double> finish() {
        for (std::size_t i = 0; i < y_.size(); ++i)
            if (norm_[i] > std::numeric_limits<double>::min()) y_[i] /= norm_[i];
        return std::move(y_);
    }

private:
    std::vector<double> y_, norm_, window_, frame_;
    std::size_t n_, hop_;
};

void lock_phases(std::vector<double>& acc, const std::vector<double>& mag, const std::vector<double>& analysis) {
    const auto bins = mag.size();
    std::vector<std::size_t> peaks;
    for (std::size_t k = 0; k < bins; ++k) {
        bool peak = true;
        for (std::size_t d = 1; d <= 2 && peak; ++d) {
            if (k >= d && !(mag[k] > mag[k - d])) peak = false;
            if (k + d < bins && !(mag[k] > mag[k + d])) peak = false;
        }
        if (peak) peaks.push_back(k);
    }
    if (peaks.empty()) return;
    std::size_t owner = 0;
    for (std::size_t k = 0; k < bins; ++k) {
        while (owner + 1 < peaks.size() && 2 * k > peaks[owner] + peaks[owner + 1]) ++owner;
        const auto p = peaks[owner];
        if (k != p) acc[k] = acc[p] + (analysis[k] - analysis[p]);
    }
}

Result<std::vector<double>> vocoder(std::span<const double> x, double rate, const StretchOptions& o, bool locked, ProgressSink* progress) {
    CenteredFrames frames{x, o.n_fft, o.hop};
    const auto n_in = frames.count();
    const auto n_out = static_cast<std::size_t>(std::ceil(static_cast<double>(n_in) / rate));
    const auto bins = o.n_fft / 2 + 1;
    OverlapAdd ola{output_length(x.size(), rate), o.n_fft, o.hop};
    std::vector<double> acc(bins), diff(bins, 0.0), mag(bins);
    std::vector<C> spectrum(bins);
    for (std::size_t j = 0; j < n_out; ++j) {
        if (progress) {
            if (progress->cancelled()) return make_error(Errc::cancelled, "time stretch: cancelled");
            progress->report(static_cast<double>(j) / static_cast<double>(n_out));
        }
        const double t = static_cast<double>(j) * rate;
        const auto i0 = static_cast<std::size_t>(std::floor(t));
        const auto i1 = std::min(i0 + 1, n_in - 1);
        const auto lo = n_in == 1 ? std::size_t{0} : std::clamp(static_cast<std::size_t>(std::ceil(t)), std::size_t{1}, n_in - 1) - 1;
        frames.forget_before(std::min(lo, i0));
        if (n_in == 1) {
            mag = frames.at(0).mag;
        } else {
            const auto& a = frames.at(lo).mag;
            const auto& b = frames.at(lo + 1).mag;
            for (std::size_t k = 0; k < bins; ++k) mag[k] = (b[k] - a[k]) * (t - static_cast<double>(lo)) + a[k];
        }
        const auto& p0 = frames.at(i0).phase;
        const auto& p1 = frames.at(i1).phase;
        if (j == 0) {
            acc = p0;
        } else {
            for (std::size_t k = 0; k < bins; ++k) acc[k] += diff[k];
            if (locked) lock_phases(acc, mag, t - static_cast<double>(i0) < 0.5 ? p0 : p1);
        }
        for (std::size_t k = 0; k < bins; ++k) diff[k] = p1[k] - p0[k];
        for (std::size_t k = 0; k < bins; ++k) spectrum[k] = mag[k] * C{std::cos(acc[k]), std::sin(acc[k])};
        ola.add(j, spectrum);
    }
    return ola.finish();
}

Result<std::vector<double>> wsola(std::span<const double> x, double rate, const StretchOptions& o, ProgressSink* progress) {
    const auto n = o.n_fft, hop = o.hop, tolerance = std::max(o.hop, o.n_fft / 4);
    const auto length = output_length(x.size(), rate);
    const auto window = periodic_hann(n);
    const auto half = static_cast<std::ptrdiff_t>(n / 2);
    const auto size = static_cast<std::ptrdiff_t>(x.size());
    auto segment = [&](std::ptrdiff_t start, std::size_t count) {
        std::vector<double> s(count, 0.0);
        for (std::size_t k = 0; k < count; ++k)
            if (const auto i = start + static_cast<std::ptrdiff_t>(k); i >= 0 && i < size) s[k] = x[static_cast<std::size_t>(i)];
        return s;
    };
    std::vector<double> y(length, 0.0), norm(length, 0.0);
    const auto frames = (length + n / 2 + hop - 1) / hop;
    std::ptrdiff_t previous = 0;
    for (std::size_t m = 0; m < frames; ++m) {
        if (progress) {
            if (progress->cancelled()) return make_error(Errc::cancelled, "time stretch: cancelled");
            progress->report(static_cast<double>(m) / static_cast<double>(frames));
        }
        const auto ideal = static_cast<std::ptrdiff_t>(std::llround(static_cast<double>(m * hop) * rate)) - half;
        std::ptrdiff_t start = ideal;
        if (m > 0) {
            const auto natural = segment(previous + static_cast<std::ptrdiff_t>(hop), n);
            const auto tol = static_cast<std::ptrdiff_t>(tolerance);
            const auto region = segment(ideal - tol, n + 2 * tolerance);
            const auto cross = correlate(region, natural, CorrelationMode::valid);
            if (!cross) return std::unexpected{cross.error()};
            std::vector<double> energy(region.size() + 1, 0.0);
            for (std::size_t k = 0; k < region.size(); ++k) energy[k + 1] = energy[k] + region[k] * region[k];
            auto score = [&](std::ptrdiff_t d) {
                const auto at = static_cast<std::size_t>(d + tol);
                const double e = energy[at + n] - energy[at];
                return e > 1e-300 ? (*cross)[at] / std::sqrt(e) : 0.0;
            };
            std::ptrdiff_t best = 0;
            double top = score(0);
            for (std::ptrdiff_t d = 1; d <= tol; ++d)
                for (const auto c : {-d, d})
                    if (const double s = score(c); s > top) {
                        top = s;
                        best = c;
                    }
            start = ideal + best;
        }
        for (std::size_t k = 0; k < n; ++k) {
            const auto pos = static_cast<std::ptrdiff_t>(m * hop + k) - half;
            if (pos < 0 || pos >= static_cast<std::ptrdiff_t>(length)) continue;
            const auto i = start + static_cast<std::ptrdiff_t>(k);
            y[static_cast<std::size_t>(pos)] += window[k] * (i >= 0 && i < size ? x[static_cast<std::size_t>(i)] : 0.0);
            norm[static_cast<std::size_t>(pos)] += window[k];
        }
        previous = start;
    }
    for (std::size_t i = 0; i < length; ++i)
        if (norm[i] > 1e-12) y[i] /= norm[i];
    return y;
}

std::optional<Error> check_options(const StretchOptions& o) {
    if (o.n_fft < 16 || o.n_fft % 2 != 0) return Error{Errc::invalid_argument, "time stretch: the window must be even and hold at least 16 samples"};
    if (o.hop == 0 || o.hop > o.n_fft / 2) return Error{Errc::invalid_argument, "time stretch: the hop must be in 1..n_fft/2"};
    return std::nullopt;
}

}  // namespace

std::string_view stretch_method_name(StretchMethod m) {
    switch (m) {
    case StretchMethod::vocoder: return "vocoder";
    case StretchMethod::locked: return "locked";
    case StretchMethod::wsola: return "wsola";
    }
    return "vocoder";
}

std::optional<StretchMethod> parse_stretch_method(std::string_view name) {
    for (const auto m : {StretchMethod::vocoder, StretchMethod::locked, StretchMethod::wsola})
        if (stretch_method_name(m) == name) return m;
    return std::nullopt;
}

Result<std::vector<double>> fft_resample(std::span<const double> x, std::size_t num) {
    if (x.empty()) return make_error(Errc::invalid_argument, "resample: the signal is empty");
    if (num == 0) return make_error(Errc::invalid_argument, "resample: the output length must be positive");
    const auto n_x = x.size();
    std::vector<C> spectrum(n_x / 2 + 1);
    pocketfft::r2c({n_x}, {sizeof(double)}, {sizeof(C)}, 0, pocketfft::FORWARD, x.data(), spectrum.data(), 1.0);
    const auto m = std::min(num, n_x);
    std::vector<C> kept(num / 2 + 1, C{});
    std::copy_n(spectrum.begin(), m / 2 + 1, kept.begin());
    if (m % 2 == 0 && num != n_x) kept[m / 2] *= num < n_x ? 2.0 : 0.5;
    const double scale = static_cast<double>(n_x) / static_cast<double>(num);
    for (auto& c : kept) c /= scale;
    std::vector<double> y(num);
    pocketfft::c2r({num}, {sizeof(C)}, {sizeof(double)}, 0, pocketfft::BACKWARD, kept.data(), y.data(), 1.0 / static_cast<double>(num));
    return y;
}

Result<std::vector<double>> time_stretch(std::span<const double> x, double rate, const StretchOptions& o, ProgressSink* progress) {
    if (x.empty()) return make_error(Errc::invalid_argument, "time stretch: the signal is empty");
    if (!(rate >= kMinStretchRate && rate <= kMaxStretchRate)) return make_error(Errc::invalid_argument, "time stretch: the rate must be in 0.1..10");
    if (auto e = check_options(o)) return std::unexpected{*e};
    if (o.method == StretchMethod::wsola) return wsola(x, rate, o, progress);
    return vocoder(x, rate, o, o.method == StretchMethod::locked, progress);
}

Result<std::vector<double>> pitch_shift(std::span<const double> x, double semitones, const StretchOptions& o, ProgressSink* progress) {
    if (!(std::abs(semitones) <= kMaxShiftSemitones)) return make_error(Errc::invalid_argument, "pitch shift: the shift must be within ±24 semitones");
    const double rate = std::pow(2.0, -semitones / 12);
    auto stretched = time_stretch(x, rate, o, progress);
    if (!stretched) return stretched;
    std::vector<double> out(x.size(), 0.0);
    if (stretched->empty()) return out;
    if (rate != 1.0) {
        stretched = fft_resample(*stretched, static_cast<std::size_t>(std::ceil(static_cast<double>(stretched->size()) * rate)));
        if (!stretched) return stretched;
    }
    std::copy_n(stretched->begin(), std::min(out.size(), stretched->size()), out.begin());
    return out;
}

}  // namespace signals::core
