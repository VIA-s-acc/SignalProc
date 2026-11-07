#include "tui/screens/spectrum_model.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::tui {

namespace {

constexpr std::size_t kMaxColumns = 4096;
constexpr std::size_t kMaxValues = std::size_t{1} << 23;
constexpr double kTiny = 1e-30;

std::vector<double> channel_of(const SignalData& s, int channel, std::size_t first = 0, std::size_t last = SIZE_MAX) {
    const auto n = s.samples.empty() ? 0 : s.samples.front().size();
    last = std::min(last, n);
    first = std::min(first, last);
    std::vector<double> x(last - first, 0.0);
    if (channel >= 0 && static_cast<std::size_t>(channel) < s.samples.size()) {
        const auto& ch = s.samples[static_cast<std::size_t>(channel)];
        for (std::size_t i = first; i < last; ++i) x[i - first] = ch[i];
        return x;
    }
    for (const auto& ch : s.samples)
        for (std::size_t i = first; i < last; ++i) x[i - first] += ch[i];
    for (double& v : x) v /= static_cast<double>(std::max<std::size_t>(1, s.samples.size()));
    return x;
}

std::vector<double> edges(const GridSpec& g, double nyquist) {
    std::vector<double> e(g.rows + 1);
    for (std::size_t r = 0; r <= g.rows; ++r) {
        const double u = static_cast<double>(r) / static_cast<double>(g.rows);
        e[r] = g.log ? g.fmin * std::pow(nyquist / g.fmin, u) : nyquist * u;
    }
    return e;
}

}  // namespace

std::vector<double> spectrogram_grid(const SpectrogramData& s, const GridSpec& g) {
    std::vector<double> out(g.rows * g.cols, g.floor_db);
    if (g.rows == 0 || g.cols == 0 || s.cols == 0 || s.bins == 0) return out;
    const double t0 = std::min(g.t0, g.t1), t1 = std::max(g.t0, g.t1), span = std::max(1e-12, t1 - t0);
    std::vector<double> col(g.cols * s.bins, 0.0);
    std::vector<std::size_t> count(g.cols, 0);
    for (std::size_t k = 0; k < s.cols; ++k) {
        const double t = s.col_time[k];
        if (t < t0 || t > t1) continue;
        const auto c = std::min(g.cols - 1, static_cast<std::size_t>((t - t0) / span * static_cast<double>(g.cols)));
        for (std::size_t b = 0; b < s.bins; ++b) col[c * s.bins + b] += s.power[k * s.bins + b];
        ++count[c];
    }
    for (std::size_t c = 0; c < g.cols; ++c) {
        if (count[c] > 0) {
            for (std::size_t b = 0; b < s.bins; ++b) col[c * s.bins + b] /= static_cast<double>(count[c]);
            continue;
        }
        const double centre = t0 + (static_cast<double>(c) + 0.5) * span / static_cast<double>(g.cols);
        const auto it = std::ranges::lower_bound(s.col_time, centre);
        auto k = static_cast<std::size_t>(it - s.col_time.begin());
        if (k == s.cols || (k > 0 && centre - s.col_time[k - 1] < s.col_time[k] - centre)) k = k == 0 ? 0 : k - 1;
        for (std::size_t b = 0; b < s.bins; ++b) col[c * s.bins + b] = s.power[k * s.bins + b];
    }
    const double nyquist = s.fs / 2, df = s.fs / static_cast<double>(s.options.n);
    const auto e = edges(g, nyquist);
    std::vector<double> value(g.rows * g.cols, 0.0);
    double peak = 0;
    for (std::size_t r = 0; r < g.rows; ++r) {
        const auto first = static_cast<std::size_t>(std::ceil(e[r] / df));
        auto last = static_cast<std::size_t>(std::ceil(e[r + 1] / df));
        if (last > s.bins) last = s.bins;
        const double centre = g.log ? std::sqrt(e[r] * e[r + 1]) : (e[r] + e[r + 1]) / 2;
        const auto nearest = std::min(s.bins - 1, static_cast<std::size_t>(std::lround(centre / df)));
        for (std::size_t c = 0; c < g.cols; ++c) {
            double v = 0;
            if (first < last) {
                for (std::size_t b = first; b < last; ++b) v += col[c * s.bins + b];
                v /= static_cast<double>(last - first);
            } else {
                v = col[c * s.bins + nearest];
            }
            value[(g.rows - 1 - r) * g.cols + c] = v;
            peak = std::max(peak, v);
        }
    }
    for (std::size_t i = 0; i < value.size(); ++i)
        out[i] = std::max(g.floor_db, peak > 0 ? 10 * std::log10(value[i] / peak + kTiny) : g.floor_db);
    return out;
}

double box_level_db(const SpectrogramData& s, double t0, double t1, double f0, double f1) {
    if (t0 > t1) std::swap(t0, t1);
    if (f0 > f1) std::swap(f0, f1);
    const double df = s.fs / static_cast<double>(s.options.n);
    double sum = 0;
    std::size_t cells = 0;
    for (std::size_t k = 0; k < s.cols; ++k) {
        if (s.col_time[k] < t0 || s.col_time[k] > t1) continue;
        for (std::size_t b = 0; b < s.bins; ++b) {
            const double f = static_cast<double>(b) * df;
            if (f < f0 || f > f1) continue;
            sum += s.power[k * s.bins + b];
            ++cells;
        }
    }
    if (cells == 0 || !(s.peak > 0)) return -INFINITY;
    return 10 * std::log10(sum / static_cast<double>(cells) / s.peak + kTiny);
}

double level_at_db(const SpectrogramData& s, double t, double f) {
    if (s.cols == 0 || !(s.peak > 0)) return -INFINITY;
    const auto it = std::ranges::lower_bound(s.col_time, t);
    auto k = std::min(s.cols - 1, static_cast<std::size_t>(it - s.col_time.begin()));
    if (k > 0 && t - s.col_time[k - 1] < s.col_time[k] - t) --k;
    const auto b = std::min(s.bins - 1, static_cast<std::size_t>(std::lround(f / (s.fs / static_cast<double>(s.options.n)))));
    return 10 * std::log10(s.power[k * s.bins + b] / s.peak + kTiny);
}

SpectrumModel::SpectrumModel(std::shared_ptr<SignalModel> source) : source_{std::move(source)} {}

void SpectrumModel::sync(JobRunner& jobs) {
    const auto s = source_->data();
    if (s && s != seen_) {
        if (seen_) requested_.reset();
        seen_ = s;
        selection_for_.reset();
        compute(jobs);
    }
    if (seen_) compute_selection(jobs);
}

void SpectrumModel::set_options(SpectrumOptions o, JobRunner& jobs) {
    if (o == options_) return;
    options_ = o;
    if (!seen_) return;
    selection_for_.reset();
    compute(jobs);
    compute_selection(jobs);
}

void SpectrumModel::set_selection(double t0, double t1, JobRunner& jobs) {
    if (t0 > t1) std::swap(t0, t1);
    if (seen_ && (t1 - t0) * seen_->info.sample_rate < 16) return;
    requested_ = std::pair{t0, t1};
    if (seen_) compute_selection(jobs);
}

std::pair<double, double> SpectrumModel::selection_range() const {
    if (requested_) return *requested_;
    if (auto s = source_->selection()) return *s;
    return {0, seen_ ? seen_->duration() : 0};
}

std::shared_ptr<const SpectrogramData> SpectrumModel::spectrogram() const {
    std::lock_guard lock{mutex_};
    return data_ && data_->options == options_ && data_->source.lock() == seen_ ? data_ : nullptr;
}

std::shared_ptr<const SelectionSpectrum> SpectrumModel::selection() const {
    std::lock_guard lock{mutex_};
    return selection_ && selection_->options == options_ && selection_->source.lock() == seen_ ? selection_ : nullptr;
}

std::optional<std::string> SpectrumModel::error() const {
    std::lock_guard lock{mutex_};
    return error_;
}

std::optional<std::uint64_t> SpectrumModel::job() const {
    std::lock_guard lock{mutex_};
    return job_ ? job_ : selection_job_;
}

bool SpectrumModel::cancelled() const {
    std::lock_guard lock{mutex_};
    return cancelled_;
}

void SpectrumModel::recompute(JobRunner& jobs) {
    if (!seen_) return;
    selection_for_.reset();
    compute(jobs);
    compute_selection(jobs);
}

void SpectrumModel::clear_selection(JobRunner& jobs) {
    requested_.reset();
    if (seen_) compute_selection(jobs);
}

void SpectrumModel::cancel(JobRunner& jobs) {
    std::lock_guard lock{mutex_};
    if (job_ || selection_job_) cancelled_ = true;
    if (job_) jobs.cancel(*job_);
    if (selection_job_) jobs.cancel(*selection_job_);
    job_.reset();
    selection_job_.reset();
    ++gen_;
    ++selection_gen_;
}

void SpectrumModel::compute(JobRunner& jobs) {
    const auto s = seen_;
    if (!s || s->samples.empty()) return;
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        if (job_) jobs.cancel(*job_);
        job_.reset();
        error_.reset();
        cancelled_ = false;
        gen = ++gen_;
    }
    const auto opts = options_;
    const auto id = jobs.submit(fmt::format(fmt::runtime(tr("tui.spectrum.job")), fmt::arg("n", opts.n)), {tr("tui.spectrum.stage.stft")},
                                [this, s, opts, gen](JobContext& ctx) {
        ctx.begin(0);
        const auto x = channel_of(*s, opts.channel);
        const auto hop = std::max<std::size_t>(1, opts.n / std::max<std::size_t>(1, opts.hop_divisor));
        auto d = std::make_shared<SpectrogramData>();
        d->options = opts;
        d->fs = s->info.sample_rate;
        d->duration = s->duration();
        d->bins = opts.n / 2 + 1;
        d->hop = hop;
        d->source = s;
        const auto frames = x.size() >= opts.n && hop > 0 ? 1 + (x.size() - opts.n) / hop : 0;
        const auto limit = std::max<std::size_t>(1, std::min(kMaxColumns, kMaxValues / d->bins));
        const auto group = std::max<std::size_t>(1, (frames + limit - 1) / limit);
        d->cols = (frames + group - 1) / group;
        d->power.assign(d->cols * d->bins, 0.0F);
        d->col_time.assign(d->cols, 0.0);
        std::vector<double> sum(d->bins);
        auto r = core::stft_frames(x, opts.n, hop, [&](std::size_t f, std::span<const double> p) {
            const auto k = f / group;
            if (f % group == 0) std::fill(sum.begin(), sum.end(), 0.0);
            for (std::size_t b = 0; b < p.size(); ++b) sum[b] += p[b];
            d->col_time[k] += (static_cast<double>(opts.n) / 2 + static_cast<double>(f * hop)) / d->fs;
            if (f % group == group - 1 || f + 1 == frames) {
                const auto members = static_cast<double>(f % group + 1);
                for (std::size_t b = 0; b < p.size(); ++b) {
                    const auto v = static_cast<float>(sum[b] / members);
                    d->power[k * d->bins + b] = v;
                    d->peak = std::max(d->peak, v);
                }
                d->col_time[k] /= members;
            }
        }, &ctx);
        if (r) {
            d->memory = core::TrackedBytes{core::MemoryCategory::display, d->power.size() * sizeof(float)};
            ctx.finish(d->power.size() * sizeof(float));
        } else {
            ctx.fail(r.error().message);
        }
        std::lock_guard lock{mutex_};
        if (gen != gen_) return;
        job_.reset();
        done_ = gen;
        if (!r) {
            if (r.error().code != core::Errc::cancelled)
                error_ = fmt::format(fmt::runtime(tr("tui.spectrum.too_short")), fmt::arg("n", opts.n), fmt::arg("frames", x.size()));
            return;
        }
        data_ = std::move(d);
    });
    std::lock_guard lock{mutex_};
    if (done_ != gen && gen_ == gen) job_ = id;
}

void SpectrumModel::compute_selection(JobRunner& jobs) {
    const auto s = seen_;
    if (!s || s->samples.empty()) return;
    const auto range = selection_range();
    if (selection_for_ == range) return;
    selection_for_ = range;
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        if (selection_job_) jobs.cancel(*selection_job_);
        selection_job_.reset();
        gen = ++selection_gen_;
    }
    const auto opts = options_;
    const auto id = jobs.submit(tr("tui.spectrum.job_selection"), {tr("tui.spectrum.stage.welch")}, [this, s, opts, range, gen](JobContext& ctx) {
        ctx.begin(0);
        const double fs = s->info.sample_rate;
        const auto first = static_cast<std::size_t>(std::max(0.0, range.first * fs));
        const auto last = static_cast<std::size_t>(std::max(0.0, range.second * fs));
        const auto x = channel_of(*s, opts.channel, first, last);
        auto out = std::make_shared<SelectionSpectrum>();
        out->t0 = range.first;
        out->t1 = range.second;
        out->options = opts;
        out->source = s;
        const auto segment = std::min<std::size_t>(16384, x.size());
        if (segment >= 16) {
            auto p = core::welch_psd(x, fs, segment, segment / 2, &ctx);
            if (p) {
                out->f = core::welch_frequencies(fs, segment);
                const double peak = *std::ranges::max_element(*p);
                out->db.resize(p->size());
                for (std::size_t i = 0; i < p->size(); ++i) out->db[i] = 10 * std::log10((*p)[i] / (peak > 0 ? peak : 1) + kTiny);
                auto peaks = core::find_peaks(out->db, 12, 8);
                std::erase_if(peaks, [&](const core::Peak& q) { return out->f[q.index] < 40 || out->f[q.index] > 8000; });
                std::ranges::stable_sort(peaks, [&](const core::Peak& a, const core::Peak& b) { return out->db[a.index] > out->db[b.index]; });
                if (peaks.size() > 6) peaks.resize(6);
                std::ranges::sort(peaks, {}, &core::Peak::index);
                out->peaks = std::move(peaks);
                out->f0 = core::estimate_f0(out->f, out->db, out->peaks);
                if (out->f0) out->harmonics = core::harmonics(out->f, out->db, *out->f0, 8);
            }
        }
        ctx.finish();
        std::lock_guard lock{mutex_};
        if (gen != selection_gen_) return;
        selection_job_.reset();
        selection_done_ = gen;
        selection_ = std::move(out);
    });
    std::lock_guard lock{mutex_};
    if (selection_done_ != gen && selection_gen_ == gen) selection_job_ = id;
}

}  // namespace signals::tui
