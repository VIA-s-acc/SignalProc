#include "tui/screens/resample_model.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "core/design/window_fir.hpp"
#include "core/multirate.hpp"
#include "core/spectral.hpp"

namespace signals::tui {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kFloorDb = -160;
constexpr std::size_t kHead = 24;

std::size_t segment_for(std::size_t n) {
    std::size_t s = 1;
    while (s * 2 <= std::min<std::size_t>(n, 1024)) s *= 2;
    return s;
}

core::Result<std::pair<std::vector<double>, std::vector<double>>> power_per_bin(std::span<const double> x, double fs) {
    const auto seg = segment_for(x.size());
    auto psd = core::welch_psd(x, fs, seg, seg / 2);
    if (!psd) return std::unexpected{psd.error()};
    for (auto& v : *psd) v *= fs / static_cast<double>(seg);
    return std::pair{core::welch_frequencies(fs, seg), std::move(*psd)};
}

double db(double power, double ref) { return power > 0 && ref > 0 ? std::max(kFloorDb, 10 * std::log10(power / ref)) : kFloorDb; }

double at_frequency(const std::vector<double>& f, const std::vector<double>& p, double x) {
    if (f.empty()) return 0;
    if (x <= f.front()) return p.front();
    if (x >= f.back()) return p.back();
    const auto it = std::upper_bound(f.begin(), f.end(), x);
    const auto j = static_cast<std::size_t>(it - f.begin());
    const double t = (x - f[j - 1]) / (f[j] - f[j - 1]);
    return p[j - 1] + t * (p[j] - p[j - 1]);
}

}  // namespace

FilterPanel filter_panel(const ResampleView& v) {
    if (v.down > v.up) return FilterPanel::before;
    if (v.up > 1) return FilterPanel::after;
    return FilterPanel::none;
}

double folded_frequency(double f, double fs_out) {
    f = std::abs(f);
    if (!(fs_out > 0)) return f;
    const double r = std::fmod(f, fs_out);
    return r > fs_out / 2 ? fs_out - r : r;
}

std::vector<double> resample_source(const ResampleOptions& o, double fs, std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) / fs;
        if (o.source == ResampleSource::sweep) {
            const double duration = static_cast<double>(n) / fs;
            x[k] = 0.5 * std::sin(kPi * (fs / 2) * t * t / duration);
        } else {
            x[k] = 0.5 * std::sin(2 * kPi * o.tone_hz * t);
        }
    }
    return x;
}

core::Result<ResampleView> compute_resample(const ResampleOptions& o, std::span<const double> x, double fs) {
    if (x.size() < 64) return core::make_error(core::Errc::invalid_argument, "resample: the signal needs at least 64 samples");
    if (!(fs > 0)) return core::make_error(core::Errc::invalid_argument, "resample: the sample rate must be positive");
    ResampleView v;
    v.fs = fs;
    v.up = o.mode == ResampleMode::down ? 1 : std::clamp<std::size_t>(o.l, 1, kMaxFactor);
    v.down = o.mode == ResampleMode::up ? 1 : std::clamp<std::size_t>(o.m, 1, kMaxFactor);
    v.fs_out = fs * static_cast<double>(v.up) / static_cast<double>(v.down);
    const auto factor = std::max(v.up, v.down);
    core::Laurent h{{static_cast<double>(v.up)}, 0};
    const core::MultirateOptions defaults;
    if (o.filter == AntiAlias::on && factor > 1) {
        const double width = defaults.transition / static_cast<double>(factor);
        const auto order = core::kaiser_order(defaults.attenuation_db, width);
        h = core::kaiser_lowpass(order.taps | 1, std::clamp(o.cutoff, 0.1, 1.0) / static_cast<double>(factor), order.beta);
        for (auto& c : h.coeffs) c *= static_cast<double>(v.up);
        constexpr std::size_t kGrid = 2048;
        const double stop = (std::clamp(o.cutoff, 0.1, 1.0) + defaults.transition / 2) / static_cast<double>(factor);
        double worst = 0, dc = 0;
        for (double c : h.coeffs) dc += c;
        for (std::size_t g = 0; g <= kGrid; ++g) {
            const double nu = static_cast<double>(g) / kGrid;
            std::complex<double> s{0, 0};
            for (std::size_t k = 0; k < h.coeffs.size(); ++k) s += h.coeffs[k] * std::polar(1.0, -kPi * nu * static_cast<double>(k));
            const double mag = std::abs(s) / std::abs(dc);
            v.filter_f.push_back(nu * fs * static_cast<double>(v.up) / 2);
            v.filter_db.push_back(std::max(kFloorDb, 20 * std::log10(std::max(mag, 1e-300))));
            if (nu >= stop) worst = std::max(worst, mag);
        }
        v.aa_attenuation_db = worst > 0 ? -20 * std::log10(worst) : -kFloorDb;
    }
    v.taps = h.coeffs.size();
    auto gain_db = [&](double f) {
        if (h.coeffs.size() < 2) return 0.0;
        const double nu = f / (fs * static_cast<double>(v.up) / 2);
        std::complex<double> s{0, 0};
        double dc = 0;
        for (std::size_t k = 0; k < h.coeffs.size(); ++k) {
            s += h.coeffs[k] * std::polar(1.0, -kPi * nu * static_cast<double>(k));
            dc += h.coeffs[k];
        }
        return std::max(kFloorDb, 20 * std::log10(std::max(std::abs(s) / std::abs(dc), 1e-300)));
    };
    if (o.source == ResampleSource::tone) {
        v.tone_gain_db = gain_db(o.tone_hz);
        if (v.up > 1) v.image_gain_db = gain_db(fs - o.tone_hz);
    }
    const auto y = core::upfirdn(h, x, v.up, v.down);
    if (y.size() < 16) return core::make_error(core::Errc::invalid_argument, "resample: the output is too short to show");
    auto in = power_per_bin(x, fs);
    auto out = power_per_bin(y, v.fs_out);
    if (!in) return std::unexpected{in.error()};
    if (!out) return std::unexpected{out.error()};
    const double ref = *std::max_element(in->second.begin(), in->second.end());
    v.f_in = std::move(in->first);
    v.f_out = std::move(out->first);
    for (double p : in->second) v.psd_in_db.push_back(db(p, ref));
    for (double p : out->second) v.psd_out_db.push_back(db(p, ref));
    if (o.mode == ResampleMode::down && o.filter == AntiAlias::bypass && v.down > 1) {
        for (std::size_t k = 0; k < v.down; ++k) {
            std::vector<double> copy;
            for (const double f : v.f_out)
                copy.push_back(db(at_frequency(v.f_in, in->second, folded_frequency(f + static_cast<double>(k) * v.fs_out, fs)) / static_cast<double>(v.down), ref));
            v.copies_db.push_back(std::move(copy));
        }
    }
    v.x_head.assign(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(std::min(kHead, x.size())));
    v.y_head.assign(y.begin(), y.begin() + static_cast<std::ptrdiff_t>(std::min(kHead * v.up / v.down + 1, y.size())));
    if (o.source == ResampleSource::tone) v.tone_out = folded_frequency(o.tone_hz, v.fs_out);
    return v;
}

}  // namespace signals::tui
