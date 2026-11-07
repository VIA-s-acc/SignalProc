#include "tui/screens/decomposition_model.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <fmt/format.h>

#include "core/bank.hpp"
#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/cwt.hpp"
#include "core/multirate.hpp"
#include "core/swt.hpp"
#include "i18n/i18n.hpp"
#include "tui/monitor/sampler.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/screen_util.hpp"

namespace signals::tui {
core::Result<core::BasicCoeffs<double>> forward_transform(const std::vector<double>& x, const core::FilterBank& bank, const DecompositionOptions& o) {
    if (o.transform == Transform::dwt) return core::wavedec<double>(x, bank, o.levels, {o.mode});
    if (x.empty()) return core::make_error(core::Errc::invalid_argument, "swt: empty signal");
    if (o.levels >= 63 || (std::size_t{1} << o.levels) > x.size()) return core::make_error(core::Errc::level_out_of_range, "swt: too many levels for the signal length");
    const std::size_t block = std::size_t{1} << o.levels;
    const auto size = (x.size() + block - 1) / block * block;
    std::vector<double> padded(size);
    for (std::size_t i = 0; i < size; ++i) padded[i] = x[i % x.size()];
    auto c = core::swt<double>(padded, bank, o.levels, {o.transform == Transform::modwt, false});
    if (c) c->original_length = x.size();
    return c;
}

core::Result<std::vector<double>> inverse_transform(const core::BasicCoeffs<double>& c, const core::FilterBank& bank, const DecompositionOptions& o) {
    if (o.transform == Transform::dwt) return core::waverec<double>(c, bank, {o.mode});
    auto y = core::iswt<double>(c, bank, {o.transform == Transform::modwt, false});
    if (y && c.original_length > 0) y->resize(c.original_length);
    return y;
}

namespace {

constexpr std::size_t kTileColumns = 512;
constexpr double kTileFloorDb = -48;

double energy(std::span<const double> v) {
    double s = 0;
    for (double c : v) s += c * c;
    return s;
}

std::vector<float> to_float(const std::vector<double>& v) { return {v.begin(), v.end()}; }

std::size_t undecimated_bytes(std::size_t n, const DecompositionOptions& o) {
    if (o.transform == Transform::dwt || o.levels >= 63) return 0;
    const std::size_t block = std::size_t{1} << o.levels;
    const auto size = (n + block - 1) / block * block;
    return (o.levels + 1) * size * 6 * sizeof(double);
}

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
    return data_job_ ? data_job_ : recon_job_ ? recon_job_ : packets_job_ ? packets_job_ : scalogram_job_;
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
    if (!data_job_ && !recon_job_ && !packets_job_ && !scalogram_job_) return;
    if (scalogram_job_) {
        jobs.cancel(*scalogram_job_);
        scalogram_error_ = tr("tui.decomposition.cancelled");
        scalogram_job_.reset();
        ++scalogram_gen_;
    }
    if (data_job_) jobs.cancel(*data_job_);
    if (recon_job_) jobs.cancel(*recon_job_);
    if (packets_job_) {
        jobs.cancel(*packets_job_);
        packets_error_ = tr("tui.decomposition.cancelled");
    }
    data_job_.reset();
    recon_job_.reset();
    packets_job_.reset();
    ++data_gen_;
    ++recon_gen_;
    ++packets_gen_;
    recon_pending_ = false;
    cancelled_ = true;
}

void DecompositionModel::recompute(JobRunner& jobs) {
    {
        std::lock_guard lock{mutex_};
        if (scalogram_error_) scalogram_for_.reset();
    }
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
        packets_.reset();
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
            d->source = s;
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
            const auto need = undecimated_bytes(n, opts);
            auto coeffs = need > budget ? core::Result<core::BasicCoeffs<double>>{core::make_error(
                                              core::Errc::invalid_argument,
                                              fmt::format(fmt::runtime(tr("tui.decomposition.swt_memory")), fmt::arg("need", format_si(static_cast<double>(need), tr("unit.byte"))),
                                                          fmt::arg("budget", format_si(static_cast<double>(budget), tr("unit.byte")))))}
                                        : forward_transform(d->x, bank, opts);
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
            const auto weights = opts.transform == Transform::dwt ? std::vector<double>{} : core::swt_energy_weights(J, opts.transform == Transform::modwt);
            const auto fractions = core::band_energy_fractions(d->coeffs, weights);
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
            if (auto back = inverse_transform(d->coeffs, bank, opts)) d->pr_error = core::max_abs_error(*back, d->x);
            else d->pr_error = INFINITY;
            double total = 0;
            for (std::size_t k = 0; k < d->coeffs.bands.size(); ++k) total += energy(d->coeffs.bands[k]) * (k < weights.size() ? weights[k] : 1.0);
            double ex = energy(d->x);
            if (opts.transform != Transform::dwt)
                for (std::size_t i = n; i < d->coeffs.bands.front().size(); ++i) ex += d->x[i % n] * d->x[i % n];
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
            auto xhat = inverse_transform(rec->processed, bank, opts);
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
                                               fmt::arg("need", format_si(static_cast<double>((J + 1) * n * sizeof(float)), tr("unit.byte"))));
                ctx.finish(0, *rec->mra_skipped);
            } else {
                for (std::size_t i = 0; i <= J && !ctx.cancelled(); ++i) {
                    auto only = rec->processed;
                    for (std::size_t b = 0; b < only.bands.size(); ++b)
                        if (b != J - i) std::fill(only.bands[b].begin(), only.bands[b].end(), 0.0);
                    auto comp = inverse_transform(only, bank, opts);
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
                                            const double sigma = d->sigma * core::swt_noise_scale(level, d->options.transform == Transform::modwt);
                                            const double lambda = core::rule_threshold(*processing.rule, band, sigma, n);
                                            rec->lambdas.push_back(lambda);
                                            if (sigma > 0) band = core::threshold(band, lambda, processing.mode);
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
                                    auto xhat = inverse_transform(kept, bank, d->options);
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
                                                                       fmt::arg("need", format_si(static_cast<double>((J + 1) * n * sizeof(float)), tr("unit.byte"))));
                                        ctx.finish(0, *rec->mra_skipped);
                                    } else {
                                        for (std::size_t i = 0; i <= J && !ctx.cancelled(); ++i) {
                                            auto only = rec->processed;
                                            for (std::size_t b = 0; b < only.bands.size(); ++b)
                                                if (b != J - i) std::fill(only.bands[b].begin(), only.bands[b].end(), 0.0);
                                            auto comp = inverse_transform(only, bank, d->options);
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

namespace {

bool is_ancestor(core::PacketNode a, core::PacketNode n) { return a.level < n.level && (n.index >> (n.level - a.level)) == a.index; }

double reconstruction_error(const PacketData& p, const std::vector<core::PacketNode>& basis, const DecompositionData& d) {
    const auto bank = core::wavelet(p.options.wavelet);
    if (!bank) return INFINITY;
    const auto y = core::wp_reconstruct(p.tree, basis, *bank, p.options.mode);
    return y ? core::max_abs_error(*y, d.x) : INFINITY;
}

}  // namespace

void DecompositionModel::request_packets(JobRunner& jobs) {
    const auto d = data();
    if (!d) return;
    const auto bank = core::wavelet(d->options.wavelet);
    if (!bank) return;
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        if (packets_ && packets_->source.lock() == d) return;
        if (packets_for_.lock() == d) return;
        if (packets_job_) jobs.cancel(*packets_job_);
        packets_job_.reset();
        packets_error_.reset();
        packets_for_ = d;
        gen = ++packets_gen_;
        const auto values = (d->options.levels + 1) * d->x.size();
        const auto need = 2 * values * sizeof(double);
        if (need > budget_ || values > (std::size_t{1} << 28)) {
            packets_error_ = fmt::format(fmt::runtime(tr("tui.decomposition.packets_memory")), fmt::arg("need", format_si(static_cast<double>(need), tr("unit.byte"))),
                                         fmt::arg("budget", format_si(static_cast<double>(budget_), tr("unit.byte"))));
            return;
        }
    }
    const auto opts = d->options;
    const auto id = jobs.submit(tr("tui.decomposition.job_packets"), {tr("tui.decomposition.stage.packets")}, [this, d, gen, opts, bank = *bank](JobContext& ctx) {
        ctx.begin(0);
        auto tree = core::wp_decompose(d->x, bank, opts.levels, opts.mode, &ctx);
        if (!tree) {
            ctx.fail(tree.error().message);
            std::lock_guard lock{mutex_};
            if (gen != packets_gen_) return;
            if (tree.error().code != core::Errc::cancelled) packets_error_ = tree.error().message;
            packets_job_.reset();
            packets_done_ = gen;
            return;
        }
        auto p = std::make_shared<PacketData>();
        p->options = opts;
        p->source = d;
        p->norm2 = energy(d->x);
        const auto J = tree->levels();
        std::size_t bytes = 0;
        p->table.resize(J + 1);
        p->level_cost.assign(J + 1, 0.0);
        for (std::size_t j = 0; j <= J; ++j) {
            const auto count = std::size_t{1} << j;
            const double width = d->fs / 2 / static_cast<double>(count);
            p->table[j].resize(count);
            for (std::size_t k = 0; k < count; ++k) {
                const core::PacketNode node{j, core::natural_index_at(j, k)};
                const auto& c = tree->at(node);
                bytes += c.size() * sizeof(double);
                const double cost = core::packet_cost(c, core::PacketCost::shannon, p->norm2);
                p->table[j][k] = {node, width * static_cast<double>(k), width * static_cast<double>(k + 1), p->norm2 > 0 ? 100 * energy(c) / p->norm2 : 0, cost};
                p->level_cost[j] += cost;
            }
        }
        p->best = core::best_basis(*tree, core::PacketCost::shannon);
        p->tree = std::move(*tree);
        for (const auto& n : p->best) p->best_cost += p->info(n).cost;
        for (const auto& n : core::dwt_basis(J)) p->dwt_cost += p->info(n).cost;
        p->root_cost = p->level_cost[0];
        p->memory = core::TrackedBytes{core::MemoryCategory::coefficients, bytes};
        ctx.finish(bytes);
        if (ctx.cancelled()) return;
        const double pr = reconstruction_error(*p, p->best, *d);
        std::lock_guard lock{mutex_};
        if (gen != packets_gen_) return;
        packets_ = std::move(p);
        basis_ = packets_->best;
        basis_pr_ = pr;
        ++basis_gen_;
        packets_job_.reset();
        packets_done_ = gen;
    });
    std::lock_guard lock{mutex_};
    if (packets_done_ != gen && packets_gen_ == gen) packets_job_ = id;
}

std::shared_ptr<const PacketData> DecompositionModel::packets() const {
    std::lock_guard lock{mutex_};
    return packets_ && data_ && packets_->source.lock() == data_ ? packets_ : nullptr;
}

std::optional<std::string> DecompositionModel::packets_error() const {
    std::lock_guard lock{mutex_};
    return packets_for_.lock() == data_ ? packets_error_ : std::nullopt;
}

std::vector<core::PacketNode> DecompositionModel::basis() const {
    std::lock_guard lock{mutex_};
    return basis_;
}

bool DecompositionModel::set_basis(std::vector<core::PacketNode> b, JobRunner& jobs) {
    const auto p = packets();
    const auto d = data();
    if (!p || !d || !core::admissible(b, p->tree.levels())) return false;
    core::sort_by_frequency(b);
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        basis_ = b;
        basis_pr_.reset();
        gen = ++basis_gen_;
    }
    jobs.submit(tr("tui.decomposition.job_basis"), {tr("tui.decomposition.stage.basis")}, [this, p, d, b = std::move(b), gen](JobContext& ctx) {
        ctx.begin(0);
        const double pr = reconstruction_error(*p, b, *d);
        ctx.finish();
        std::lock_guard lock{mutex_};
        if (gen == basis_gen_) basis_pr_ = pr;
    });
    return true;
}

void DecompositionModel::make_leaf(core::PacketNode n, JobRunner& jobs) {
    const auto p = packets();
    if (!p || n.level > p->tree.levels() || n.index >= (std::size_t{1} << n.level)) return;
    auto b = basis();
    if (std::ranges::find(b, n) != b.end()) return;
    std::optional<core::PacketNode> ancestor;
    std::erase_if(b, [&](core::PacketNode m) {
        if (is_ancestor(m, n)) ancestor = m;
        return is_ancestor(m, n) || is_ancestor(n, m);
    });
    b.push_back(n);
    if (ancestor)
        for (std::size_t l = ancestor->level + 1; l <= n.level; ++l) b.push_back({l, (n.index >> (n.level - l)) ^ 1U});
    set_basis(std::move(b), jobs);
}

double DecompositionModel::basis_cost() const {
    const auto p = packets();
    if (!p) return 0;
    double total = 0;
    for (const auto& n : basis()) total += p->info(n).cost;
    return total;
}

std::optional<double> DecompositionModel::basis_pr_error() const {
    std::lock_guard lock{mutex_};
    return basis_pr_;
}

core::Result<ScalogramData> compute_scalogram(std::span<const double> x, double fs, std::size_t cols, core::ProgressSink* progress, std::size_t budget) {
    constexpr std::size_t kMaxSamples = std::size_t{1} << 20;
    constexpr double kVoicesPerOctave = 8, kLowestHz = 20, kFloorDb = -48;
    if (x.size() < 64 || !(fs > 0) || cols == 0) return core::make_error(core::Errc::invalid_argument, tr("tui.decomposition.scalogram_short"));
    ScalogramData out;
    out.decimation = (x.size() + kMaxSamples - 1) / kMaxSamples;
    std::vector<double> signal;
    if (out.decimation > 1) {
        core::MultirateOptions aa;
        aa.attenuation_db = 60;
        const auto h = core::anti_alias_filter(out.decimation, aa).coeffs;
        const auto delay = (h.size() - 1) / 2;
        signal.resize((x.size() + out.decimation - 1) / out.decimation);
        for (std::size_t k = 0; k < signal.size(); ++k) {
            if (progress && (k & 0xFFFF) == 0 && progress->cancelled()) return core::make_error(core::Errc::cancelled, "scalogram: cancelled");
            const auto centre = k * out.decimation + delay;
            const auto first = centre >= x.size() ? centre - x.size() + 1 : 0;
            const auto last = std::min(h.size(), centre + 1);
            double acc = 0;
            for (std::size_t j = first; j < last; ++j) acc += h[j] * x[centre - j];
            signal[k] = acc;
        }
    } else {
        signal.assign(x.begin(), x.end());
    }
    const double rate = fs / static_cast<double>(out.decimation);
    const auto n = signal.size();
    const auto w = *core::continuous_wavelet("morl");
    const double fc = core::center_frequency(w);
    const double hi = 0.4 * rate, lo = std::max(kLowestHz, 16 * fc * rate / static_cast<double>(n));
    if (!(lo < hi)) return core::make_error(core::Errc::invalid_argument, tr("tui.decomposition.scalogram_short"));
    out.rows = std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(kVoicesPerOctave * std::log2(hi / lo))) + 1, 2, 128);
    out.cols = std::min(cols, n);
    const auto longest = static_cast<std::size_t>(16 * fc * rate / lo) + 2;
    const auto need = 56 * (n + longest) + out.rows * out.cols * (2 * sizeof(double) + 1);
    if (budget > 0 && need > budget)
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.decomposition.scalogram_memory")),
                                                                          fmt::arg("need", format_si(static_cast<double>(need), tr("unit.byte"))),
                                                                          fmt::arg("budget", format_si(static_cast<double>(budget), tr("unit.byte")))));
    std::vector<double> scales(out.rows);
    for (std::size_t r = 0; r < out.rows; ++r) {
        out.freq_hz.push_back(hi * std::pow(lo / hi, static_cast<double>(r) / static_cast<double>(out.rows - 1)));
        scales[r] = fc * rate / out.freq_hz.back();
    }
    std::vector<double> sum(out.rows * out.cols, 0.0);
    std::vector<std::size_t> count(out.cols, 0);
    for (std::size_t t = 0; t < n; ++t) ++count[t * out.cols / n];
    auto ok = core::cwt_rows(signal, scales, w, [&](std::size_t r, std::span<const std::complex<double>> row) {
        auto* line = sum.data() + r * out.cols;
        for (std::size_t t = 0; t < n; ++t) line[t * out.cols / n] += std::norm(row[t]);
    }, progress);
    if (!ok) return std::unexpected{ok.error()};
    double peak = 0;
    for (std::size_t i = 0; i < sum.size(); ++i) {
        sum[i] /= static_cast<double>(std::max<std::size_t>(1, count[i % out.cols]));
        peak = std::max(peak, sum[i]);
    }
    out.db.resize(sum.size());
    out.coi.resize(sum.size());
    for (std::size_t r = 0; r < out.rows; ++r)
        for (std::size_t c = 0; c < out.cols; ++c) {
            const auto i = r * out.cols + c;
            out.db[i] = peak > 0 && sum[i] > 0 ? std::max(kFloorDb, 10 * std::log10(sum[i] / peak)) : kFloorDb;
            const double centre = (static_cast<double>(c) + 0.5) * static_cast<double>(n) / static_cast<double>(out.cols);
            const double edge = std::min(centre, static_cast<double>(n) - centre);
            out.coi[i] = edge < std::sqrt(2.0) * scales[r] ? 1 : 0;
        }
    out.memory = core::TrackedBytes{core::MemoryCategory::display, out.db.size() * (sizeof(double) + 1)};
    return out;
}

void DecompositionModel::request_scalogram(JobRunner& jobs) {
    const auto d = data();
    const auto s = source_->data();
    if (!d || !s) return;
    std::uint64_t gen = 0;
    {
        std::lock_guard lock{mutex_};
        if (d->source.lock() != s) return;
        if (scalogram_for_.lock() == s && scalogram_channel_ == d->options.channel) return;
        scalogram_.reset();
        if (scalogram_job_) jobs.cancel(*scalogram_job_);
        scalogram_job_.reset();
        scalogram_error_.reset();
        scalogram_for_ = s;
        scalogram_channel_ = d->options.channel;
        gen = ++scalogram_gen_;
    }
    constexpr std::size_t kScalogramColumns = 512;
    const auto id = jobs.submit(tr("tui.decomposition.job_scalogram"), {tr("tui.decomposition.stage.scalogram")}, [this, d, s, gen, budget = budget_](JobContext& ctx) {
        ctx.begin(0);
        auto out = compute_scalogram(d->x, d->fs, kScalogramColumns, &ctx, budget);
        if (!out) {
            ctx.fail(out.error().message);
            std::lock_guard lock{mutex_};
            if (gen != scalogram_gen_) return;
            if (out.error().code != core::Errc::cancelled) scalogram_error_ = out.error().message;
            scalogram_job_.reset();
            scalogram_done_ = gen;
            return;
        }
        out->signal = s;
        out->channel = d->options.channel;
        ctx.finish(out->db.size() * sizeof(double));
        auto published = std::make_shared<const ScalogramData>(std::move(*out));
        std::lock_guard lock{mutex_};
        if (gen != scalogram_gen_) return;
        scalogram_ = std::move(published);
        scalogram_job_.reset();
        scalogram_done_ = gen;
    });
    std::lock_guard lock{mutex_};
    if (scalogram_done_ != gen && scalogram_gen_ == gen) scalogram_job_ = id;
}

std::shared_ptr<const ScalogramData> DecompositionModel::scalogram() const {
    const auto s = source_->data();
    std::lock_guard lock{mutex_};
    return scalogram_ && s && scalogram_->signal.lock() == s && scalogram_->channel == options_.channel ? scalogram_ : nullptr;
}

std::optional<std::string> DecompositionModel::scalogram_error() const {
    std::lock_guard lock{mutex_};
    return scalogram_error_;
}

}  // namespace signals::tui
