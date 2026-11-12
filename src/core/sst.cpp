#include "core/sst.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <numbers>
#include <string>
#include <thread>

#include "core/detail/pocketfft.hpp"

namespace signals::core::sst {
namespace {

using C = std::complex<double>;
using Batch = std::function<void(std::size_t first, std::span<const std::span<const C>> wx, std::span<const std::span<const C>> dwx)>;

constexpr double kGamma = 10 * std::numeric_limits<double>::epsilon();
constexpr double kTwoPi = 6.283185307179586;
constexpr double kMargin = 6;
constexpr std::size_t kMaxPadded = std::size_t{1} << 28;
constexpr std::size_t kMinSegment = std::size_t{1} << 15;
constexpr double kMaxSample = 1e150;
constexpr std::size_t kThreads = 8;
constexpr std::size_t kBatchBytes = std::size_t{128} << 20;
constexpr std::size_t kChunk = 4096;

Error bad(std::string text) { return {Errc::invalid_argument, "sst: " + std::move(text)}; }

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

Result<void> check_band(double lo, double hi) {
    if (std::isnan(lo) || std::isnan(hi) || lo < 0 || !(hi > lo)) return std::unexpected{bad("the band needs 0 ≤ lo < hi")};
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

struct Morlet {
    double mu, ks, gain;
    explicit Morlet(double m)
        : mu{m}, ks{std::exp(-0.5 * m * m)},
          gain{std::sqrt(2.0) * std::pow(1 + std::exp(-m * m) - 2 * std::exp(-0.75 * m * m), -0.5) * std::pow(std::numbers::pi, 0.25)} {}
    double operator()(double w) const { return gain * (std::exp(-0.5 * (w - mu) * (w - mu)) - ks * std::exp(-0.5 * w * w)); }
    double reach() const { return mu + 40; }
};

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

std::size_t thread_count() { return std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, kThreads); }

template <class F>
void parallel(std::size_t count, const F& job) {
    const auto threads = std::min(count, thread_count());
    if (threads <= 1) {
        for (std::size_t j = 0; j < count; ++j) job(j);
        return;
    }
    std::atomic<std::size_t> next{0};
    std::exception_ptr failure;
    std::mutex failure_mutex;
    const auto worker = [&] {
        try {
            for (auto j = next.fetch_add(1); j < count; j = next.fetch_add(1)) job(j);
        } catch (...) {
            std::lock_guard lock{failure_mutex};
            if (!failure) failure = std::current_exception();
            next.store(count);
        }
    };
    std::vector<std::thread> pool;
    for (std::size_t t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
    if (failure) std::rethrow_exception(failure);
}

template <class F>
void over_time(std::size_t from, std::size_t to, const F& job) {
    const auto parts = std::clamp<std::size_t>((to - from) / kChunk, 1, thread_count());
    parallel(parts, [&](std::size_t p) { job(from + p * (to - from) / parts, from + (p + 1) * (to - from) / parts); });
}

struct Workspace {
    std::vector<C> xh, block, tx;
    std::vector<double> xi;
    std::vector<std::span<const C>> wx, dwx;
};

std::size_t batch_of(std::size_t up) { return std::clamp<std::size_t>(kBatchBytes / (2 * up * sizeof(C)), 1, thread_count()); }

Result<void> rows(std::span<const double> x, const Options& o, const Batch& batch_fn, ProgressSink* progress, Pad pad, Workspace& ws) {
    if (auto ok = check(x, o); !ok) return ok;
    const auto n = x.size();
    const auto up = pad.up;
    const pocketfft::shape_t shape{up};
    const pocketfft::stride_t stride{sizeof(C)};
    auto& xh = ws.xh;
    xh.resize(up);
    for (std::size_t i = 0; i < up; ++i) xh[i] = reflect(x, static_cast<long long>(i) - static_cast<long long>(pad.left));
    pocketfft::c2c(shape, stride, stride, {0}, pocketfft::FORWARD, xh.data(), xh.data(), 1.0);
    auto& xi = ws.xi;
    xi.resize(up);
    const double h = kTwoPi / static_cast<double>(up);
    for (std::size_t i = 0; i < up; ++i) xi[i] = (i <= up / 2 ? static_cast<double>(i) : static_cast<double>(i) - static_cast<double>(up)) * h;
    const auto s = scales(o);
    const Morlet psi{o.mu};
    const auto batch = batch_of(up);
    auto& block = ws.block;
    block.resize(2 * batch * up);
    auto& wx = ws.wx;
    auto& dwx = ws.dwx;
    wx.resize(batch);
    dwx.resize(batch);
    for (std::size_t a0 = 0; a0 < s.size(); a0 += batch) {
        if (progress && progress->cancelled()) return std::unexpected{Error{Errc::cancelled, "sst: cancelled"}};
        const auto count = std::min(batch, s.size() - a0);
        parallel(count, [&](std::size_t j) {
            auto* buffer = block.data() + 2 * j * up;
            auto* slope = buffer + up;
            const double scale = s[a0 + j], reach = psi.reach() / scale;
            for (std::size_t i = 0; i < up; ++i) {
                if (std::abs(xi[i]) > reach) {
                    buffer[i] = slope[i] = C{};
                    continue;
                }
                double psih = psi(scale * xi[i]);
                if (up % 2 == 0 && i == up / 2) psih /= 2;
                buffer[i] = psih * xh[i];
                slope[i] = buffer[i] * C{0, xi[i]};
            }
            pocketfft::c2c(shape, stride, stride, {0}, pocketfft::BACKWARD, buffer, buffer, 1.0 / static_cast<double>(up));
            pocketfft::c2c(shape, stride, stride, {0}, pocketfft::BACKWARD, slope, slope, 1.0 / static_cast<double>(up));
        });
        for (std::size_t j = 0; j < count; ++j) {
            wx[j] = std::span<const C>{block.data() + 2 * j * up + pad.left, n};
            dwx[j] = std::span<const C>{block.data() + (2 * j + 1) * up + pad.left, n};
        }
        batch_fn(a0, std::span{wx}.first(count), std::span{dwx}.first(count));
        if (progress) progress->report(static_cast<double>(a0 + count) / static_cast<double>(s.size()));
    }
    return {};
}

Result<void> squeeze(std::span<const double> x, const Options& o, const Bins& bins, ProgressSink* progress, Pad pad, std::size_t keep_from,
                     std::size_t keep_to, Workspace& ws) {
    const Mapper bin{bins};
    const double weight = std::numbers::ln2 / o.voices;
    const auto kept = keep_to - keep_from;
    ws.tx.assign(bins.freqs.size() * kept, C{});
    auto* tx = ws.tx.data();
    return rows(x, o, [&](std::size_t, std::span<const std::span<const C>> wx, std::span<const std::span<const C>> dwx) {
        over_time(keep_from, keep_to, [&](std::size_t from, std::size_t to) {
            for (std::size_t j = 0; j < wx.size(); ++j)
                for (std::size_t t = from; t < to; ++t)
                    if (std::abs(wx[j][t]) > kGamma) tx[bin(frequency(wx[j][t], dwx[j][t])) * kept + t - keep_from] += wx[j][t] * weight;
        });
    }, progress, pad, ws);
}

struct Segment {
    std::size_t from = 0, to = 0, keep_from = 0, keep_to = 0;
    Pad pad;
};

std::vector<Segment> segments(std::size_t n, const Options& o, std::size_t length) {
    const auto margin = static_cast<std::size_t>(std::ceil(kMargin * scales(o).back())) + 1;
    if (length == 0) length = std::max(4 * margin, kMinSegment);
    if (n <= length) return {{0, n, 0, n, pad_of(n)}};
    std::vector<Segment> out;
    for (std::size_t start = 0; start < n; start += length) {
        const auto stop = std::min(n, start + length);
        const auto from = start > margin ? start - margin : 0;
        const auto to = std::min(n, stop + margin);
        out.push_back({from, to, start - from, stop - from, Pad{pocketfft::detail::util::good_size_cmplx(to - from + 2 * margin), margin}});
    }
    return out;
}

template <class F>
Result<void> each_segment(std::span<const double> x, const Options& o, std::size_t length, ProgressSink* progress, const F& job) {
    if (auto ok = check(x, o); !ok) return ok;
    const auto n = static_cast<double>(x.size());
    Workspace ws;
    for (const auto& seg : segments(x.size(), o, length)) {
        Part part{progress, static_cast<double>(seg.from + seg.keep_from) / n, static_cast<double>(seg.keep_to - seg.keep_from) / n};
        if (auto ok = job(seg, x.subspan(seg.from, seg.to - seg.from), &part, ws); !ok) return ok;
    }
    return {};
}

}  // namespace

double morlet_hat(double w, double mu) { return Morlet{mu}(w); }

double admissibility(double mu) {
    const Morlet psi{mu};
    const auto integrand = [&](double w) {
        if (mu * w < 1) return psi.gain * std::exp(-0.5 * (w * w + mu * mu)) * std::expm1(mu * w) / w;
        return psi.gain * (std::exp(-0.5 * (w - mu) * (w - mu)) - std::exp(-0.5 * (w * w + mu * mu))) / w;
    };
    static constexpr double node[8] = {0.0950125098376374, 0.2816035507792589, 0.4580167776572274, 0.6178762444026438,
                                       0.7554044083550030, 0.8656312023878318, 0.9445750230732326, 0.9894009349916499};
    static constexpr double weight[8] = {0.1894506104550685, 0.1826034150449236, 0.1691565193950025, 0.1495959888165767,
                                         0.1246289712555339, 0.0951585116824928, 0.0622535239386479, 0.0271524594117541};
    constexpr int kPanels = 4000;
    const double h = psi.reach() / kPanels;
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
    o.s0 = peak_frequency(mu) / (kTwoPi * hi);
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
    if (x.empty()) return std::unexpected{bad("empty signal")};
    Workspace ws;
    return rows(x, o, [&](std::size_t first, std::span<const std::span<const C>> wx, std::span<const std::span<const C>> dwx) {
        for (std::size_t j = 0; j < wx.size(); ++j) row(first + j, wx[j], dwx[j]);
    }, progress, pad_of(x.size()), ws);
}

Result<Plane> transform(std::span<const double> x, const Options& o, const Bins& bins, ProgressSink* progress) {
    if (auto ok = check(bins); !ok) return std::unexpected{ok.error()};
    if (auto ok = check(x, o); !ok) return std::unexpected{ok.error()};
    Workspace ws;
    if (auto ok = squeeze(x, o, bins, progress, pad_of(x.size()), 0, x.size(), ws); !ok) return std::unexpected{ok.error()};
    Plane tx(bins.freqs.size());
    for (std::size_t k = 0; k < tx.size(); ++k) tx[k].assign(ws.tx.begin() + static_cast<std::ptrdiff_t>(k * x.size()), ws.tx.begin() + static_cast<std::ptrdiff_t>((k + 1) * x.size()));
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

Result<std::vector<double>> band(std::span<const double> x, const Options& o, double lo, double hi, ProgressSink* progress, std::size_t segment) {
    if (auto ok = check_band(lo, hi); !ok) return std::unexpected{ok.error()};
    const double weight = std::numbers::ln2 / o.voices;
    std::vector<double> out(x.size(), 0.0);
    auto ok = each_segment(x, o, segment, progress, [&](const Segment& seg, std::span<const double> window, ProgressSink* part, Workspace& ws) {
        return rows(window, o, [&](std::size_t, std::span<const std::span<const C>> wx, std::span<const std::span<const C>> dwx) {
            over_time(seg.keep_from, seg.keep_to, [&](std::size_t from, std::size_t to) {
                for (std::size_t j = 0; j < wx.size(); ++j)
                    for (std::size_t t = from; t < to; ++t) {
                        if (!(std::abs(wx[j][t]) > kGamma)) continue;
                        const double w = frequency(wx[j][t], dwx[j][t]);
                        if (w >= lo && w <= hi) out[seg.from + t] += wx[j][t].real() * weight;
                    }
            });
        }, part, seg.pad, ws);
    });
    if (!ok) return std::unexpected{ok.error()};
    const double gain = 2 / admissibility(o.mu);
    for (auto& v : out) v *= gain;
    return out;
}

Result<Ridge> ridge(std::span<const double> x, const Options& o, double lo, double hi, ProgressSink* progress, std::size_t segment) {
    if (auto ok = check_band(lo, hi); !ok) return std::unexpected{ok.error()};
    Ridge out{std::vector<double>(x.size(), 0.0), std::vector<double>(x.size(), 0.0)};
    auto ok = each_segment(x, o, segment, progress, [&](const Segment& seg, std::span<const double> window, ProgressSink* part, Workspace& ws) {
        return rows(window, o, [&](std::size_t, std::span<const std::span<const C>> wx, std::span<const std::span<const C>> dwx) {
            over_time(seg.keep_from, seg.keep_to, [&](std::size_t from, std::size_t to) {
                for (std::size_t j = 0; j < wx.size(); ++j)
                    for (std::size_t t = from; t < to; ++t) {
                        const double m = std::abs(wx[j][t]);
                        auto& best = out.amp[seg.from + t];
                        if (!(m > kGamma) || !(m > best)) continue;
                        const double w = frequency(wx[j][t], dwx[j][t]);
                        if (w >= lo && w <= hi) best = m, out.freq[seg.from + t] = w;
                    }
            });
        }, part, seg.pad, ws);
    });
    if (!ok) return std::unexpected{ok.error()};
    const double gain = 2 / morlet_hat(peak_frequency(o.mu), o.mu);
    for (auto& v : out.amp) v *= gain;
    return out;
}

Result<PowerMap> power_map(std::span<const double> x, const Options& o, const Bins& bins, std::size_t cols, ProgressSink* progress, std::size_t segment) {
    if (cols == 0) return std::unexpected{bad("no columns")};
    if (auto ok = check(bins); !ok) return std::unexpected{ok.error()};
    const auto n = x.size();
    PowerMap map{bins.freqs.size(), std::min(cols, n), {}};
    map.power.assign(map.rows * map.cols, 0.0);
    std::vector<std::size_t> count(map.cols, 0);
    auto ok = each_segment(x, o, segment, progress, [&](const Segment& seg, std::span<const double> window, ProgressSink* part, Workspace& ws) -> Result<void> {
        if (auto done = squeeze(window, o, bins, part, seg.pad, seg.keep_from, seg.keep_to, ws); !done) return done;
        const auto kept = seg.keep_to - seg.keep_from;
        for (std::size_t t = seg.keep_from; t < seg.keep_to; ++t) {
            const auto c = (seg.from + t) * map.cols / n;
            ++count[c];
            for (std::size_t r = 0; r < map.rows; ++r) map.power[r * map.cols + c] += std::norm(ws.tx[r * kept + t - seg.keep_from]);
        }
        return {};
    });
    if (!ok) return std::unexpected{ok.error()};
    for (std::size_t r = 0; r < map.rows; ++r)
        for (std::size_t c = 0; c < map.cols; ++c) map.power[r * map.cols + c] /= static_cast<double>(std::max<std::size_t>(1, count[c]));
    return map;
}

std::size_t map_bytes(std::size_t n, const Options& o, std::size_t bins) {
    const auto seg = segments(n, o, 0);
    std::size_t up = 0, kept = 0;
    for (const auto& s : seg) up = std::max(up, s.pad.up), kept = std::max(kept, s.keep_to - s.keep_from);
    return bins * kept * sizeof(C) + 3 * batch_of(up) * up * sizeof(C) + up * (sizeof(C) + sizeof(double));
}

}  // namespace signals::core::sst
