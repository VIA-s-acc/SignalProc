#include "tui/screens/decomposition_model.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <fmt/format.h>

#include "core/bank.hpp"
#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "i18n/i18n.hpp"
#include "tui/monitor/sampler.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/screen_util.hpp"

namespace signals::tui {
namespace {

constexpr std::size_t kTileColumns = 512;
constexpr double kTileFloorDb = -48;

double energy(std::span<const double> v) {
    double s = 0;
    for (double c : v) s += c * c;
    return s;
}

std::vector<float> to_float(const std::vector<double>& v) { return {v.begin(), v.end()}; }

}  // namespace

DecompositionModel::DecompositionModel(std::shared_ptr<SignalModel> source, std::size_t memory_budget)
    : source_{std::move(source)}, budget_{memory_budget ? memory_budget : static_cast<std::size_t>(physical_memory_bytes() / 2)} {}

std::shared_ptr<const DecompositionData> DecompositionModel::data() const {
    std::lock_guard lock{mutex_};
    return data_;
}

std::shared_ptr<const Reconstruction> DecompositionModel::recon() const {
    std::lock_guard lock{mutex_};
    return recon_;
}

std::optional<std::string> DecompositionModel::error() const {
    std::lock_guard lock{mutex_};
    return error_;
}

std::optional<std::uint64_t> DecompositionModel::job() const {
    std::lock_guard lock{mutex_};
    return data_job_ ? data_job_ : recon_job_;
}

bool DecompositionModel::cancelled() const {
    std::lock_guard lock{mutex_};
    return cancelled_;
}

void DecompositionModel::sync(JobRunner& jobs) {
    const auto s = source_->data();
    if (s && s != seen_) {
        seen_ = s;
        compute(jobs);
        return;
    }
    bool pending = false;
    {
        std::lock_guard lock{mutex_};
        pending = recon_pending_ && !data_job_ && data_;
        if (pending) recon_pending_ = false;
    }
    if (pending) reconstruct(jobs);
}

void DecompositionModel::set_options(DecompositionOptions o, JobRunner& jobs) {
    if (auto bank = core::wavelet(o.wavelet); !bank) {
        std::lock_guard lock{mutex_};
        error_ = bank.error().message;
        return;
    }
    {
        std::lock_guard lock{mutex_};
        error_.reset();
    }
    options_ = std::move(o);
    if (seen_) compute(jobs);
}

void DecompositionModel::cancel(JobRunner& jobs) {
    std::lock_guard lock{mutex_};
    if (!data_job_ && !recon_job_) return;
    if (data_job_) jobs.cancel(*data_job_);
    if (recon_job_) jobs.cancel(*recon_job_);
    data_job_.reset();
    recon_job_.reset();
    ++data_gen_;
    ++recon_gen_;
    recon_pending_ = false;
    cancelled_ = true;
}

void DecompositionModel::recompute(JobRunner& jobs) {
    if (seen_) compute(jobs);
}

void DecompositionModel::compute(JobRunner& jobs) {
    const auto s = seen_;
    if (!s || s->samples.empty()) return;
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        if (data_job_) jobs.cancel(*data_job_);
        if (recon_job_) jobs.cancel(*recon_job_);
        data_job_.reset();
        recon_job_.reset();
        gen = ++data_gen_;
        ++recon_gen_;
        recon_pending_ = false;
        cancelled_ = false;
    }
    const auto bank = core::wavelet(options_.wavelet);
    if (!bank) {
        std::lock_guard lock{mutex_};
        error_ = bank.error().message;
        return;
    }
    const auto max_level = core::dwt_max_level(static_cast<std::size_t>(s->info.frames), bank->length());
    options_.levels = std::clamp<std::size_t>(options_.levels, 1, std::max<std::size_t>(1, max_level));
    if (options_.channel >= static_cast<int>(s->samples.size())) options_.channel = -1;
    enabled_.assign(options_.levels + 1, true);
    processing_ = {};
    processing_.last_level = std::min<std::size_t>(4, options_.levels);
    previous_.reset();
    const auto opts = options_;
    const auto name = s->path.filename().string();
    const auto id = jobs.submit(
        fmt::format(fmt::runtime(tr("tui.decomposition.job")), fmt::arg("name", name), fmt::arg("wavelet", opts.wavelet)),
        {"DWT", tr("tui.decomposition.stage.tiles"), tr("tui.decomposition.stage.recon"), "MRA"},
        [this, s, opts, gen, bank = *bank, budget = budget_](JobContext& ctx) {
            auto d = std::make_shared<DecompositionData>();
            d->options = opts;
            d->fs = s->info.sample_rate;
            ctx.begin(0);
            const auto n = s->samples.front().size();
            d->x.assign(n, 0.0);
            if (opts.channel >= 0) {
                const auto& ch = s->samples[static_cast<std::size_t>(opts.channel)];
                std::copy(ch.begin(), ch.end(), d->x.begin());
            } else {
                for (const auto& ch : s->samples)
                    for (std::size_t i = 0; i < n; ++i) d->x[i] += ch[i];
                for (double& v : d->x) v /= static_cast<double>(s->samples.size());
            }
            auto coeffs = core::wavedec<double>(d->x, bank, opts.levels, {opts.mode});
            if (!coeffs) {
                ctx.fail(coeffs.error().message);
                std::lock_guard lock{mutex_};
                if (gen != data_gen_) return;
                error_ = coeffs.error().message;
                data_job_.reset();
                data_done_ = gen;
                return;
            }
            d->coeffs = std::move(*coeffs);
            ctx.finish();
            if (ctx.cancelled()) return;
            ctx.begin(1);
            const auto J = opts.levels;
            const auto fractions = core::band_energy_fractions(d->coeffs);
            std::size_t coeff_bytes = 0;
            for (std::size_t i = 0; i <= J; ++i) {
                const auto k = J - i;
                const auto [lo, hi] = band_range(k, J, d->fs);
                d->bands.push_back({(k == 0 ? "a" : "d") + std::to_string(k == 0 ? J : i + 1), lo, hi, 100 * fractions[k], d->coeffs.bands[k].size()});
                std::vector<float> a(d->coeffs.bands[k].size());
                for (std::size_t c = 0; c < a.size(); ++c) a[c] = static_cast<float>(std::abs(d->coeffs.bands[k][c]));
                d->band_abs.push_back(std::move(a));
                coeff_bytes += d->coeffs.bands[k].size() * sizeof(double);
            }
            d->x_float = to_float(d->x);
            d->x_pyramid.emplace(d->x_float);
            std::size_t display = d->x_pyramid->bytes();
            for (const auto& a : d->band_abs) {
                d->band_pyramids.emplace_back(a);
                display += d->band_pyramids.back().bytes();
            }
            double peak = 0;
            for (const auto& a : d->band_abs)
                for (float v : a) peak = std::max(peak, static_cast<double>(v));
            d->tiles = {J + 1, kTileColumns, std::vector<double>((J + 1) * kTileColumns, kTileFloorDb)};
            for (std::size_t r = 0; r <= J; ++r) {
                const auto& a = d->band_abs[r];
                for (std::size_t c = 0; c < a.size(); ++c) {
                    const auto col = std::min(kTileColumns - 1, c * kTileColumns / a.size());
                    auto& cell = d->tiles.db[r * kTileColumns + col];
                    const double db = peak > 0 && a[c] > 0 ? 20 * std::log10(a[c] / peak) : kTileFloorDb;
                    cell = std::max(cell, std::max(kTileFloorDb, db));
                }
            }
            if (auto back = core::waverec<double>(d->coeffs, bank, {opts.mode})) d->pr_error = core::max_abs_error(*back, d->x);
            else d->pr_error = INFINITY;
            double total = 0;
            for (const auto& b : d->coeffs.bands) total += energy(b);
            const double ex = energy(d->x);
            d->parseval_error = ex > 0 ? std::abs(total - ex) / ex : 0;
            d->sigma = core::noise_sigma_mad(d->coeffs.bands.back());
            d->orthogonal = core::check_orthogonality(bank, 1e-9).passed();
            d->memory = core::TrackedBytes{core::MemoryCategory::coefficients, coeff_bytes + n * (sizeof(double) + sizeof(float))};
            d->display_memory = core::TrackedBytes{core::MemoryCategory::display, display};
            ctx.finish(coeff_bytes);
            if (ctx.cancelled()) return;
            std::shared_ptr<const DecompositionData> published = d;
            {
                std::lock_guard lock{mutex_};
                if (gen != data_gen_) return;
                data_ = published;
                recon_.reset();
                error_.reset();
            }
            Processing p;
            p.last_level = std::min<std::size_t>(4, J);
            auto rec = std::make_shared<Reconstruction>();
            rec->enabled.assign(J + 1, true);
            rec->processing = p;
            ctx.begin(2);
            rec->processed = published->coeffs;
            auto xhat = core::waverec<double>(rec->processed, bank, {opts.mode});
            if (!xhat) {
                ctx.fail(xhat.error().message);
                return;
            }
            xhat->resize(n);
            rec->xhat = to_float(*xhat);
            rec->difference.resize(n);
            for (std::size_t i = 0; i < n; ++i) rec->difference[i] = static_cast<float>(published->x[i] - (*xhat)[i]);
            const double ex2 = energy(published->x);
            rec->energy_kept_pct = ex2 > 0 ? 100 * energy(*xhat) / ex2 : 100;
            rec->xhat_pyramid.emplace(rec->xhat);
            ctx.finish();
            ctx.begin(3);
            if ((J + 1) * n * sizeof(float) > budget) {
                rec->mra_skipped = fmt::format(fmt::runtime(tr("tui.decomposition.mra_skipped")),
                                               fmt::arg("need", format_si(static_cast<double>((J + 1) * n * sizeof(float)), "Б")));
                ctx.finish(0, *rec->mra_skipped);
            } else {
                for (std::size_t i = 0; i <= J && !ctx.cancelled(); ++i) {
                    auto only = rec->processed;
                    for (std::size_t b = 0; b < only.bands.size(); ++b)
                        if (b != J - i) std::fill(only.bands[b].begin(), only.bands[b].end(), 0.0);
                    auto comp = core::waverec<double>(only, bank, {opts.mode});
                    if (!comp) break;
                    comp->resize(n);
                    rec->components.push_back(to_float(*comp));
                    ctx.report(static_cast<double>(i + 1) / static_cast<double>(J + 1));
                }
                for (const auto& c : rec->components) rec->component_pyramids.emplace_back(c);
                ctx.finish((J + 1) * n * sizeof(float));
            }
            if (ctx.cancelled()) return;
            rec->memory = core::TrackedBytes{core::MemoryCategory::display, (rec->components.size() + 2) * n * sizeof(float)};
            std::lock_guard lock{mutex_};
            if (gen != data_gen_) return;
            if (!recon_pending_) recon_ = std::move(rec);
            data_job_.reset();
            data_done_ = gen;
        });
    std::lock_guard lock{mutex_};
    if (data_done_ != gen && data_gen_ == gen) data_job_ = id;
}

void DecompositionModel::reconstruct(JobRunner& jobs) {
    const auto d = data();
    if (!d) return;
    const auto bank = core::wavelet(d->options.wavelet);
    if (!bank) return;
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        if (data_job_) {
            recon_pending_ = true;
            return;
        }
        if (recon_job_) jobs.cancel(*recon_job_);
        recon_job_.reset();
        gen = ++recon_gen_;
        cancelled_ = false;
    }
    const auto enabled = enabled_;
    const auto processing = processing_;
    const auto id = jobs.submit(tr("tui.decomposition.job_recon"), {tr("tui.decomposition.stage.recon"), "MRA"},
                                [this, d, gen, enabled, processing, bank = *bank, budget = budget_](JobContext& ctx) {
                                    ctx.begin(0);
                                    const auto J = d->options.levels;
                                    const auto n = d->x.size();
                                    auto rec = std::make_shared<Reconstruction>();
                                    rec->enabled = enabled;
                                    rec->processing = processing;
                                    rec->processed = d->coeffs;
                                    auto& bands = rec->processed.bands;
                                    if (processing.rule) {
                                        const auto last = std::min(processing.last_level, J);
                                        for (std::size_t level = std::max<std::size_t>(1, processing.first_level); level <= last; ++level) {
                                            auto& band = bands[J + 1 - level];
                                            const double lambda = *processing.rule == core::ShrinkRule::bayes ? core::bayes_threshold(band, d->sigma)
                                                                                                             : core::universal_threshold(d->sigma, n);
                                            rec->lambdas.push_back(lambda);
                                            if (d->sigma > 0) band = core::threshold(band, lambda, processing.mode);
                                        }
                                    }
                                    if (processing.keep_fraction) core::keep_largest(bands, *processing.keep_fraction, 1);
                                    for (std::size_t i = 0; i < processing.gains_db.size() && i <= J; ++i)
                                        if (processing.gains_db[i] != 0) {
                                            const double g = std::pow(10.0, processing.gains_db[i] / 20);
                                            for (double& c : bands[J - i]) c *= g;
                                        }
                                    auto kept = rec->processed;
                                    for (std::size_t i = 0; i <= J && i < enabled.size(); ++i)
                                        if (!enabled[i]) std::fill(kept.bands[J - i].begin(), kept.bands[J - i].end(), 0.0);
                                    auto xhat = core::waverec<double>(kept, bank, {d->options.mode});
                                    if (!xhat) {
                                        ctx.fail(xhat.error().message);
                                        std::lock_guard lock{mutex_};
                                        if (gen != recon_gen_) return;
                                        recon_job_.reset();
                                        recon_done_ = gen;
                                        return;
                                    }
                                    xhat->resize(n);
                                    rec->xhat = to_float(*xhat);
                                    rec->difference.resize(n);
                                    for (std::size_t i = 0; i < n; ++i) rec->difference[i] = static_cast<float>(d->x[i] - (*xhat)[i]);
                                    const double ex = energy(d->x);
                                    rec->energy_kept_pct = ex > 0 ? 100 * energy(*xhat) / ex : 100;
                                    rec->xhat_pyramid.emplace(rec->xhat);
                                    ctx.finish();
                                    if (ctx.cancelled()) return;
                                    ctx.begin(1);
                                    if ((J + 1) * n * sizeof(float) > budget) {
                                        rec->mra_skipped = fmt::format(fmt::runtime(tr("tui.decomposition.mra_skipped")),
                                                                       fmt::arg("need", format_si(static_cast<double>((J + 1) * n * sizeof(float)), "Б")));
                                        ctx.finish(0, *rec->mra_skipped);
                                    } else {
                                        for (std::size_t i = 0; i <= J && !ctx.cancelled(); ++i) {
                                            auto only = rec->processed;
                                            for (std::size_t b = 0; b < only.bands.size(); ++b)
                                                if (b != J - i) std::fill(only.bands[b].begin(), only.bands[b].end(), 0.0);
                                            auto comp = core::waverec<double>(only, bank, {d->options.mode});
                                            if (!comp) break;
                                            comp->resize(n);
                                            rec->components.push_back(to_float(*comp));
                                        }
                                        for (const auto& c : rec->components) rec->component_pyramids.emplace_back(c);
                                        ctx.finish();
                                    }
                                    if (ctx.cancelled()) return;
                                    rec->memory = core::TrackedBytes{core::MemoryCategory::display, (rec->components.size() + 2) * n * sizeof(float)};
                                    std::lock_guard lock{mutex_};
                                    if (gen != recon_gen_ || data_ != d) return;
                                    recon_ = std::move(rec);
                                    recon_job_.reset();
                                    recon_done_ = gen;
                                });
    std::lock_guard lock{mutex_};
    if (recon_done_ != gen && recon_gen_ == gen) recon_job_ = id;
}

void DecompositionModel::remember() { previous_ = std::pair{enabled_, processing_}; }

void DecompositionModel::toggle(std::size_t band, JobRunner& jobs) {
    if (band >= enabled_.size()) return;
    remember();
    enabled_[band] = !enabled_[band];
    reconstruct(jobs);
}

void DecompositionModel::set_enabled(std::vector<bool> enabled, JobRunner& jobs) {
    if (enabled.size() != enabled_.size()) return;
    remember();
    enabled_ = std::move(enabled);
    reconstruct(jobs);
}

void DecompositionModel::apply(Processing p, JobRunner& jobs) {
    remember();
    processing_ = std::move(p);
    reconstruct(jobs);
}

void DecompositionModel::undo(JobRunner& jobs) {
    if (!previous_) return;
    enabled_ = std::move(previous_->first);
    processing_ = std::move(previous_->second);
    previous_.reset();
    reconstruct(jobs);
}

std::optional<audio::Clip> DecompositionModel::clip(ClipKind kind, std::size_t band) const {
    const auto d = data();
    const auto r = recon();
    if (!d || !r) return std::nullopt;
    audio::Clip c;
    c.rate = static_cast<std::uint32_t>(std::lround(d->fs));
    switch (kind) {
    case ClipKind::original:
        c.channels.emplace_back(d->x_float);
        c.owner = d;
        c.tag = "decomposition.original";
        break;
    case ClipKind::enabled:
        c.channels.emplace_back(r->xhat);
        c.owner = r;
        c.tag = "decomposition.enabled";
        break;
    case ClipKind::difference:
        c.channels.emplace_back(r->difference);
        c.owner = r;
        c.tag = "decomposition.difference";
        break;
    case ClipKind::band:
        if (band >= r->components.size()) return std::nullopt;
        c.channels.emplace_back(r->components[band]);
        c.owner = r;
        c.tag = "decomposition.band";
        break;
    }
    return c;
}

}  // namespace signals::tui
