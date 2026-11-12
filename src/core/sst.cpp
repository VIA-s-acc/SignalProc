#include "core/sst.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>

#include "core/detail/pocketfft.hpp"

namespace signals::core::sst {
namespace {

using C = std::complex<double>;

constexpr double kGamma = 10 * std::numeric_limits<double>::epsilon();
constexpr double kTwoPi = 6.283185307179586;
constexpr double kMargin = 6;
constexpr std::size_t kMaxPadded = std::size_t{1} << 28;
constexpr std::size_t kMinSegment = std::size_t{1} << 15;
constexpr double kMaxSample = 1e150;

core::Error bad(std::string text) { return {Errc::invalid_argument, "sst: " + std::move(text)}; }

struct Pad {
    std::size_t up = 0, left = 0;
};

Pad pad_of(std::size_t n) {
    const auto octaves = 1 + static_cast<unsigned>(std::nearbyint(std::log2(static_cast<double>(n))));
    const std::size_t up = std::size_t{1} << octaves;
    const std::size_t right = (up - n) / 2;
    return {up, up - n - right};
}

double reflect(std::span<const double> x, long long j) {
    const auto n = static_cast<long long>(x.size());
    if (n == 1) return x[0];
    const long long period = 2 * (n - 1);
    long long m = j % period;
    if (m < 0) m += period;
    return x[static_cast<std::size_t>(m < n ? m : period - m)];
}

Result<void> check(std::span<const double> x, const Options& o) {
    if (x.empty()) return std::unexpected{bad("empty signal")};
    if (!(o.mu > 0) || !std::isfinite(o.mu)) return std::unexpected{bad("mu must be positive")};
    if (!(o.s0 > 0) || !std::isfinite(o.s0)) return std::unexpected{bad("the smallest scale must be positive")};
    if (o.voices <= 0) return std::unexpected{bad("voices per octave must be positive")};
    if (o.count == 0) return std::unexpected{bad("no scales")};
    if (!std::isfinite(scales(o).back())) return std::unexpected{bad("scales overflow")};
    if (x.size() > kMaxPadded / 4) return std::unexpected{bad("signal too long")};
    if (std::any_of(x.begin(), x.end(), [](double v) { return !std::isfinite(v); })) return std::unexpected{bad("the signal has NaN or infinite samples")};
    if (std::any_of(x.begin(), x.end(), [](double v) { return std::abs(v) > kMaxSample; })) return std::unexpected{bad("samples above 1e150 would overflow")};
    return {};
}

Result<void> check(const Bins& b) {
    if (b.freqs.size() < 2) return std::unexpected{bad("at least two frequency bins are needed")};
    for (std::size_t k = 0; k < b.freqs.size(); ++k) {
        if (!std::isfinite(b.freqs[k])) return std::unexpected{bad("frequency bins must be finite")};
        if (k > 0 && !(b.freqs[k] > b.freqs[k - 1])) return std::unexpected{bad("frequency bins must ascend")};
    }
    if (b.spacing == Spacing::log && !(b.freqs.front() > 0)) return std::unexpected{bad("log bins must start above zero")};
    return {};
}

struct Mapper {
    bool log = true;
    double v0 = 0, dv = 1;
    double omax = 0;

    explicit Mapper(const Bins& b) : log{b.spacing == Spacing::log}, omax{static_cast<double>(b.freqs.size() - 1)} {
        v0 = log ? std::log2(b.freqs[0]) : b.freqs[0];
        dv = log ? std::log2(b.freqs[1]) - v0 : b.freqs[1] - b.freqs[0];
    }

    std::size_t operator()(double w) const {
        double at = std::nearbyint(((log ? std::log2(w) : w) - v0) / dv);
        if (!(at > 0)) at = 0;
        if (!(at < omax)) at = omax;
        return static_cast<std::size_t>(at);
    }
};

double frequency(C wx, C dwx) {
    const double a = dwx.real(), b = dwx.imag(), c = wx.real(), d = wx.imag();
    return std::abs((b * c - a * d) / ((c * c + d * d) * kTwoPi));
}

class Part final : public ProgressSink {
public:
    Part(ProgressSink* outer, double from, double share) : outer_{outer}, from_{from}, share_{share} {}
    void report(double fraction) override {
        if (outer_) outer_->report(from_ + share_ * fraction);
    }
    bool cancelled() const override { return outer_ && outer_->cancelled(); }

private:
    ProgressSink* outer_;
    double from_, share_;
};

}  // namespace

double morlet_hat(double w, double mu) {
    const double cs = std::pow(1 + std::exp(-mu * mu) - 2 * std::exp(-0.75 * mu * mu), -0.5);
    const double ks = std::exp(-0.5 * mu * mu);
    const double gain = std::sqrt(2.0) * cs * std::pow(std::numbers::pi, 0.25);
    return gain * (std::exp(-0.5 * (w - mu) * (w - mu)) - ks * std::exp(-0.5 * w * w));
}

double admissibility(double mu) {
    const double cs = std::pow(1 + std::exp(-mu * mu) - 2 * std::exp(-0.75 * mu * mu), -0.5);
    const double gain = std::sqrt(2.0) * cs * std::pow(std::numbers::pi, 0.25);
    const auto integrand = [&](double w) {
        if (mu * w < 1) return gain * std::exp(-0.5 * (w * w + mu * mu)) * std::expm1(mu * w) / w;
        return gain * (std::exp(-0.5 * (w - mu) * (w - mu)) - std::exp(-0.5 * (w * w + mu * mu))) / w;
    };
    static constexpr double node[8] = {0.0950125098376374, 0.2816035507792589, 0.4580167776572274, 0.6178762444026438,
                                       0.7554044083550030, 0.8656312023878318, 0.9445750230732326, 0.9894009349916499};
    static constexpr double weight[8] = {0.1894506104550685, 0.1826034150449236, 0.1691565193950025, 0.1495959888165767,
                                         0.1246289712555339, 0.0951585116824928, 0.0622535239386479, 0.0271524594117541};
    constexpr int kPanels = 4000;
    const double top = mu + 40, h = top / kPanels;
    double sum = 0;
    for (int p = 0; p < kPanels; ++p) {
        const double mid = (p + 0.5) * h, half = 0.5 * h;
        for (int k = 0; k < 8; ++k) sum += weight[k] * half * (integrand(mid - half * node[k]) + integrand(mid + half * node[k]));
    }
    return sum;
}

double peak_frequency(double mu) {
    const double ks = std::exp(-0.5 * mu * mu);
    double w = mu;
    for (int k = 0; k < 200; ++k) {
        const double next = mu + ks * w * std::exp(0.5 * (w - mu) * (w - mu) - 0.5 * w * w);
        if (std::abs(next - w) < 1e-15 * mu) return next;
        w = next;
    }
    return w;
}

std::vector<double> scales(const Options& o) {
    std::vector<double> s(o.count);
    for (std::size_t i = 0; i < o.count; ++i) s[i] = o.s0 * std::pow(2.0, static_cast<double>(i) / o.voices);
    return s;
}

Options covering(double lo, double hi, int voices, double mu) {
    Options o{mu, 0, voices, 0};
    if (!(lo > 0) || !(hi > lo) || voices <= 0 || !(mu > 0)) return o;
    const double wp = peak_frequency(mu);
    o.s0 = wp / (kTwoPi * hi);
    o.count = static_cast<std::size_t>(std::floor(voices * std::log2(hi / lo) + 1e-9)) + 1;
    return o;
}

Bins log_bins(double lo, double hi, std::size_t count) {
    Bins b{std::vector<double>(count), Spacing::log};
    for (std::size_t k = 0; k < count; ++k)
        b.freqs[k] = count == 1 ? lo : lo * std::pow(hi / lo, static_cast<double>(k) / static_cast<double>(count - 1));
    if (count > 1) b.freqs.back() = hi;
    return b;
}

Result<void> cwt_rows(std::span<const double> x, const Options& o, const Row& row, ProgressSink* progress) {
    if (auto ok = check(x, o); !ok) return ok;
    const auto n = x.size();
    const auto pad = pad_of(n);
    const auto up = pad.up;
    std::vector<C> xh(up), buffer(up), slope(up);
    for (std::size_t i = 0; i < up; ++i) xh[i] = reflect(x, static_cast<long long>(i) - static_cast<long long>(pad.left));
    const pocketfft::shape_t shape{up};
    const pocketfft::stride_t stride{sizeof(C)};
    pocketfft::c2c(shape, stride, stride, {0}, pocketfft::FORWARD, xh.data(), xh.data(), 1.0);
    std::vector<double> xi(up);
    const double h = kTwoPi / static_cast<double>(up);
    for (std::size_t i = 0; i < up; ++i) xi[i] = (i <= up / 2 ? static_cast<double>(i) : static_cast<double>(i) - static_cast<double>(up)) * h;
    const auto s = scales(o);
    for (std::size_t a = 0; a < s.size(); ++a) {
        if (progress && progress->cancelled()) return std::unexpected{Error{Errc::cancelled, "sst: cancelled"}};
        for (std::size_t i = 0; i < up; ++i) {
            double psih = morlet_hat(s[a] * xi[i], o.mu);
            if (i == up / 2) psih /= 2;
            buffer[i] = psih * xh[i];
            slope[i] = buffer[i] * C{0, xi[i]};
        }
        pocketfft::c2c(shape, stride, stride, {0}, pocketfft::BACKWARD, buffer.data(), buffer.data(), 1.0 / static_cast<double>(up));
        pocketfft::c2c(shape, stride, stride, {0}, pocketfft::BACKWARD, slope.data(), slope.data(), 1.0 / static_cast<double>(up));
        row(a, std::span<const C>{buffer}.subspan(pad.left, n), std::span<const C>{slope}.subspan(pad.left, n));
        if (progress) progress->report(static_cast<double>(a + 1) / static_cast<double>(s.size()));
    }
    return {};
}

Result<Plane> transform(std::span<const double> x, const Options& o, const Bins& bins, ProgressSink* progress) {
    if (auto ok = check(bins); !ok) return std::unexpected{ok.error()};
    if (auto ok = check(x, o); !ok) return std::unexpected{ok.error()};
    const Mapper bin{bins};
    const double weight = std::numbers::ln2 / o.voices;
    Plane tx(bins.freqs.size(), std::vector<C>(x.size()));
    auto ok = cwt_rows(x, o, [&](std::size_t, std::span<const C> wx, std::span<const C> dwx) {
        for (std::size_t t = 0; t < wx.size(); ++t)
            if (std::abs(wx[t]) > kGamma) tx[bin(frequency(wx[t], dwx[t]))][t] += wx[t] * weight;
    }, progress);
    if (!ok) return std::unexpected{ok.error()};
    return tx;
}

std::vector<double> inverse(const Plane& tx, double mu) {
    std::vector<double> x(tx.empty() ? 0 : tx.front().size(), 0.0);
    for (const auto& row : tx)
        for (std::size_t t = 0; t < x.size(); ++t) x[t] += row[t].real();
    const double gain = 2 / admissibility(mu);
    for (auto& v : x) v *= gain;
    return x;
}

Result<std::vector<double>> band(std::span<const double> x, const Options& o, double lo, double hi, ProgressSink* progress) {
    if (std::isnan(lo) || std::isnan(hi) || lo < 0 || !(hi > lo)) return std::unexpected{bad("the band needs 0 ≤ lo < hi")};
    const double weight = std::numbers::ln2 / o.voices;
    std::vector<double> out(x.size(), 0.0);
    auto ok = cwt_rows(x, o, [&](std::size_t, std::span<const C> wx, std::span<const C> dwx) {
        for (std::size_t t = 0; t < wx.size(); ++t) {
            if (!(std::abs(wx[t]) > kGamma)) continue;
            const double w = frequency(wx[t], dwx[t]);
            if (w >= lo && w <= hi) out[t] += wx[t].real() * weight;
        }
    }, progress);
    if (!ok) return std::unexpected{ok.error()};
    const double gain = 2 / admissibility(o.mu);
    for (auto& v : out) v *= gain;
    return out;
}

Result<PowerMap> power_map(std::span<const double> x, const Options& o, const Bins& bins, std::size_t cols, ProgressSink* progress, std::size_t segment) {
    if (cols == 0) return std::unexpected{bad("no columns")};
    if (auto ok = check(bins); !ok) return std::unexpected{ok.error()};
    if (auto ok = check(x, o); !ok) return std::unexpected{ok.error()};
    const auto n = x.size();
    PowerMap map{bins.freqs.size(), std::min(cols, n), {}};
    map.power.assign(map.rows * map.cols, 0.0);
    std::vector<std::size_t> count(map.cols, 0);
    const auto margin = static_cast<std::size_t>(std::ceil(kMargin * scales(o).back())) + 1;
    const auto length = segment ? segment : std::max(4 * margin, kMinSegment);
    for (std::size_t start = 0; start < n; start += length) {
        const auto stop = std::min(n, start + length);
        const auto from = start > margin ? start - margin : 0;
        const auto to = std::min(n, stop + margin);
        Part part{progress, static_cast<double>(start) / static_cast<double>(n), static_cast<double>(stop - start) / static_cast<double>(n)};
        auto tx = transform(x.subspan(from, to - from), o, bins, &part);
        if (!tx) return std::unexpected{tx.error()};
        for (std::size_t t = start; t < stop; ++t) {
            const auto c = t * map.cols / n;
            ++count[c];
            for (std::size_t r = 0; r < map.rows; ++r) map.power[r * map.cols + c] += std::norm((*tx)[r][t - from]);
        }
    }
    for (std::size_t r = 0; r < map.rows; ++r)
        for (std::size_t c = 0; c < map.cols; ++c) map.power[r * map.cols + c] /= static_cast<double>(std::max<std::size_t>(1, count[c]));
    return map;
}

}  // namespace signals::core::sst
