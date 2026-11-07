#include "tui/screens/processing_model.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>

#include <fmt/format.h>

#include "core/design/families.hpp"
#include "core/detail/pocketfft.hpp"
#include "core/wavedec.hpp"
#include "i18n/i18n.hpp"

namespace signals::tui {

namespace {

double energy(std::span<const double> v) {
    double s = 0;
    for (const double x : v) s += x * x;
    return s;
}

double snr_db(std::span<const double> ref, std::span<const double> est) {
    double err = 0;
    for (std::size_t i = 0; i < ref.size() && i < est.size(); ++i) err += (ref[i] - est[i]) * (ref[i] - est[i]);
    const double sig = energy(ref);
    if (err == 0) return sig == 0 ? 0.0 : INFINITY;
    if (sig == 0) return -INFINITY;
    return 10 * std::log10(sig / err);
}

double error_db(std::span<const double> noisy, std::span<const double> y) {
    double err = 0;
    for (std::size_t i = 0; i < noisy.size() && i < y.size(); ++i) err += (noisy[i] - y[i]) * (noisy[i] - y[i]);
    const double den = energy(noisy);
    if (den == 0 || err == 0) return den == 0 ? 0.0 : -300.0;
    return 10 * std::log10(err / den);
}

std::vector<std::size_t> curve_points(std::size_t total) {
    std::vector<std::size_t> k;
    for (int i = 0; i < 60; ++i)
        k.push_back(static_cast<std::size_t>(std::exp(std::log(static_cast<double>(total)) * i / 59.0)));
    for (std::size_t d = 1; d < total; d *= 10) k.push_back(d);
    k.push_back(total);
    std::ranges::sort(k);
    const auto [first, last] = std::ranges::unique(k);
    k.erase(first, last);
    std::erase(k, std::size_t{0});
    return k;
}

}  // namespace

std::vector<double> seeded_noise(std::size_t n, double sigma, std::uint64_t seed) {
    std::uint64_t state = seed;
    auto next = [&] {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        return (static_cast<double>(z >> 11) + 0.5) / 9007199254740992.0;
    };
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; i += 2) {
        const double r = std::sqrt(-2 * std::log(next())), a = 2 * std::numbers::pi * next();
        out[i] = sigma * r * std::cos(a);
        if (i + 1 < n) out[i + 1] = sigma * r * std::sin(a);
    }
    return out;
}

core::Result<ProcessingResult> process_fragment(std::span<const double> clean, std::span<const double> noisy, double fs, const ProcessingOptions& o,
                                                core::ProgressSink* progress) {
    const auto bank = core::wavelet(o.wavelet);
    if (!bank) return std::unexpected{bank.error()};
    if (clean.size() != noisy.size() || noisy.empty()) return core::make_error(core::Errc::length_mismatch, "processing: clean and noisy fragments differ");
    const auto n = noisy.size();
    auto coeffs = core::wavedec<double>(noisy, *bank, o.levels, {o.mode}, progress);
    if (!coeffs) return std::unexpected{coeffs.error()};
    ProcessingResult r;
    r.options = o;
    r.fs = fs;
    r.clean.assign(clean.begin(), clean.end());
    r.noisy.assign(noisy.begin(), noisy.end());
    const auto J = o.levels;
    r.sigma = core::noise_sigma_mad(coeffs->bands.back());
    auto processed = *coeffs;
    for (std::size_t b = 0; b <= J; ++b) {
        const auto& band = coeffs->bands[b];
        BandResult info;
        info.name = b == 0 ? "a" + std::to_string(J) : "d" + std::to_string(J + 1 - b);
        const auto j = b == 0 ? J : J + 1 - b;
        info.lo_hz = b == 0 ? 0.0 : fs / std::ldexp(1.0, static_cast<int>(j + 1));
        info.hi_hz = fs / std::ldexp(1.0, static_cast<int>(b == 0 ? J + 1 : j));
        info.length = band.size();
        info.coeffs = band;
        for (const double c : band) info.max_abs = std::max(info.max_abs, std::abs(c));
        if (b == 0) {
            info.kept = band.size();
        } else {
            info.lambda = o.scale * core::rule_threshold(o.rule, band, r.sigma, n);
            processed.bands[b] = core::threshold(band, info.lambda, o.threshold);
            info.kept = static_cast<std::size_t>(std::ranges::count_if(band, [&](double c) { return std::abs(c) >= info.lambda; }));
        }
        r.kept += info.kept;
        r.total += band.size();
        const double g = b < o.gains.size() ? o.gains[b] : 1.0;
        if (g != 1.0)
            for (double& c : processed.bands[b]) c *= g;
        r.bands.push_back(std::move(info));
    }
    auto y = core::waverec<double>(processed, *bank, {o.mode});
    if (!y) return std::unexpected{y.error()};
    y->resize(n);
    r.out = std::move(*y);
    r.residual.resize(n);
    for (std::size_t i = 0; i < n; ++i) r.residual[i] = r.noisy[i] - r.out[i];
    r.snr_in = snr_db(r.clean, r.noisy);
    r.snr_out = snr_db(r.clean, r.out);
    double mse = 0;
    for (std::size_t i = 0; i < n; ++i) mse += (r.out[i] - r.clean[i]) * (r.out[i] - r.clean[i]);
    r.mse = mse / static_cast<double>(n);
    const double noisy_energy = energy(r.noisy);
    r.energy_pct = noisy_energy > 0 ? 100 * energy(r.out) / noisy_energy : 100.0;
    r.err_db = error_db(r.noisy, r.out);
    std::vector<double> flat;
    for (const auto& band : coeffs->bands) flat.insert(flat.end(), band.begin(), band.end());
    std::vector<std::size_t> order(flat.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return std::abs(flat[a]) > std::abs(flat[b]); });
    auto kept = *coeffs;
    for (auto& band : kept.bands) std::fill(band.begin(), band.end(), 0.0);
    std::vector<std::pair<std::size_t, std::size_t>> where(flat.size());
    for (std::size_t b = 0, at = 0; b < coeffs->bands.size(); ++b)
        for (std::size_t i = 0; i < coeffs->bands[b].size(); ++i) where[at++] = {b, i};
    std::size_t filled = 0;
    for (const auto k : curve_points(r.total)) {
        if (progress && progress->cancelled()) return core::make_error(core::Errc::cancelled, "processing: cancelled");
        for (; filled < k && filled < order.size(); ++filled) {
            const auto [b, i] = where[order[filled]];
            kept.bands[b][i] = coeffs->bands[b][i];
        }
        auto part = core::waverec<double>(kept, *bank, {o.mode});
        if (!part) return std::unexpected{part.error()};
        part->resize(n);
        r.curve.push_back({k, error_db(r.noisy, *part)});
    }
    std::vector<double> gains(J + 1, 1.0);
    for (std::size_t b = 0; b < o.gains.size() && b <= J; ++b) gains[b] = o.gains[b];
    const auto bins = transfer_bins(1024);
    if (auto t = band_transfer(o.wavelet, J, gains, 1024, bins)) r.transfer = std::move(*t);
    return r;
}

std::vector<std::size_t> transfer_bins(std::size_t np) {
    std::vector<std::size_t> out;
    const double lo = std::log(4.0), hi = std::log(static_cast<double>(np / 2 - 1));
    for (int i = 0; i < 120; ++i) out.push_back(static_cast<std::size_t>(std::exp(lo + (hi - lo) * i / 119.0)));
    out.back() = np / 2 - 1;
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

core::Result<TransferCurves> band_transfer(std::string_view wavelet, std::size_t levels, std::span<const double> gains, std::size_t np,
                                           std::span<const std::size_t> bins) {
    const auto bank = core::wavelet(std::string{wavelet});
    if (!bank) return std::unexpected{bank.error()};
    const std::size_t period = std::size_t{1} << levels;
    if (np % period != 0) return core::make_error(core::Errc::invalid_argument, "transfer: the probe length must be a multiple of 2^J");
    TransferCurves out;
    std::vector<double> part(np);
    std::vector<std::complex<double>> y(np), spectrum(np);
    const pocketfft::shape_t shape{np};
    const pocketfft::stride_t stride{sizeof(std::complex<double>)};
    for (const auto m : bins) {
        for (int which = 0; which < 2; ++which) {
            for (std::size_t k = 0; k < np; ++k) {
                const double a = 2 * std::numbers::pi * static_cast<double>(m * k) / static_cast<double>(np);
                part[k] = which == 0 ? std::cos(a) : std::sin(a);
            }
            auto c = core::wavedec<double>(part, *bank, levels, {core::Extension::periodization});
            if (!c) return std::unexpected{c.error()};
            for (std::size_t b = 0; b < c->bands.size() && b < gains.size(); ++b)
                for (double& v : c->bands[b]) v *= gains[b];
            auto rec = core::waverec<double>(*c, *bank, {core::Extension::periodization});
            if (!rec) return std::unexpected{rec.error()};
            for (std::size_t k = 0; k < np; ++k) y[k] = which == 0 ? std::complex<double>{(*rec)[k], 0} : y[k] + std::complex<double>{0, (*rec)[k]};
        }
        pocketfft::c2c(shape, stride, stride, {0}, pocketfft::FORWARD, y.data(), spectrum.data(), 1.0 / static_cast<double>(np));
        double alias = 0;
        for (std::size_t j = 1; j < period; ++j) alias += std::norm(spectrum[(m + j * np / period) % np]);
        out.omega.push_back(2.0 * static_cast<double>(m) / static_cast<double>(np));
        out.t_db.push_back(20 * std::log10(std::abs(spectrum[m]) + 1e-12));
        out.a_db.push_back(10 * std::log10(alias + 1e-24));
    }
    return out;
}

ProcessingModel::ProcessingModel(std::shared_ptr<SignalModel> source) : source_{std::move(source)} {}

void ProcessingModel::sync(JobRunner& jobs) {
    const auto s = source_->data();
    if (s && s != seen_) {
        seen_ = s;
        compute(jobs);
    }
}

void ProcessingModel::set_options(ProcessingOptions o, JobRunner& jobs) {
    if (o == options_) return;
    options_ = std::move(o);
    if (seen_) compute(jobs);
}

std::shared_ptr<const ProcessingResult> ProcessingModel::result() const {
    std::lock_guard lock{mutex_};
    return result_ && result_->source.lock() == seen_ ? result_ : nullptr;
}

std::optional<std::string> ProcessingModel::error() const {
    std::lock_guard lock{mutex_};
    return error_;
}

std::optional<std::uint64_t> ProcessingModel::job() const {
    std::lock_guard lock{mutex_};
    return job_;
}

void ProcessingModel::cancel(JobRunner& jobs) {
    std::lock_guard lock{mutex_};
    if (job_) jobs.cancel(*job_);
    job_.reset();
    ++gen_;
}

void ProcessingModel::compute(JobRunner& jobs) {
    const auto s = seen_;
    if (!s || s->samples.empty()) return;
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        if (job_) jobs.cancel(*job_);
        job_.reset();
        error_.reset();
        gen = ++gen_;
    }
    const auto o = options_;
    const auto id = jobs.submit(tr("tui.processing.job"), {tr("tui.processing.stage")}, [this, s, o, gen](JobContext& ctx) {
        ctx.begin(0);
        const auto total = s->samples.front().size();
        const auto length = std::min(o.length, total);
        const auto start = o.start + length <= total ? o.start : total - length;
        std::vector<double> clean(length, 0.0);
        if (o.channel >= 0 && static_cast<std::size_t>(o.channel) < s->samples.size()) {
            const auto& ch = s->samples[static_cast<std::size_t>(o.channel)];
            for (std::size_t i = 0; i < length; ++i) clean[i] = ch[start + i];
        } else {
            for (const auto& ch : s->samples)
                for (std::size_t i = 0; i < length; ++i) clean[i] += ch[start + i] / static_cast<double>(s->samples.size());
        }
        const double power = length ? energy(clean) / static_cast<double>(length) : 0.0;
        auto noise = seeded_noise(length, std::sqrt(power / std::pow(10.0, o.snr_db / 10)), 7);
        std::vector<double> noisy(length);
        for (std::size_t i = 0; i < length; ++i) noisy[i] = clean[i] + noise[i];
        auto r = process_fragment(clean, noisy, s->info.sample_rate, o, &ctx);
        if (r) {
            r->source = s;
            ctx.finish();
        } else {
            ctx.fail(r.error().message);
        }
        std::lock_guard lock{mutex_};
        if (gen != gen_) return;
        job_.reset();
        done_ = gen;
        if (!r) {
            if (r.error().code != core::Errc::cancelled)
                error_ = fmt::format(fmt::runtime(tr("tui.processing.error")), fmt::arg("reason", r.error().message));
            return;
        }
        result_ = std::make_shared<const ProcessingResult>(std::move(*r));
    });
    std::lock_guard lock{mutex_};
    if (done_ != gen && gen_ == gen) job_ = id;
}

}  // namespace signals::tui
