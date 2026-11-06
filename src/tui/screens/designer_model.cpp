#include "tui/screens/designer_model.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include "core/laurent.hpp"
#include "i18n/i18n.hpp"

namespace signals::tui {
namespace {

core::Laurent group_factor(const core::RootGroup& g) {
    const double re = static_cast<double>(g.root.re), im = static_cast<double>(g.root.im);
    if (!g.conjugate_pair) return {{1.0, -re}, 0};
    return {{1.0, -2 * re, re * re + im * im}, 0};
}

core::Laurent normalised(const core::Laurent& p) {
    double sum = 0;
    for (double c : p.coeffs) sum += c;
    return core::scale(p, std::sqrt(2.0) / sum);
}

core::Result<core::FilterBank> fast_bank(const core::HalfbandFactorisation& f, const core::Split& s) {
    const core::Laurent one_plus{{1.0, 1.0}, 0};
    auto h0 = core::power(one_plus, s.zeros_h0);
    auto f0 = core::power(one_plus, 2 * f.p - s.zeros_h0);
    for (std::size_t g = 0; g < f.groups.size(); ++g) {
        if (s.group_in_h0[g]) h0 = core::multiply(h0, group_factor(f.groups[g]));
        else f0 = core::multiply(f0, group_factor(f.groups[g]));
    }
    return core::make_bank<double>("split", normalised(h0), normalised(f0), 1e-8);
}

double modulus(const core::RootGroup& g) {
    const double re = static_cast<double>(g.root.re), im = static_cast<double>(g.root.im);
    return std::hypot(re, im);
}

}  // namespace

core::Split preset_split(const core::HalfbandFactorisation& f, Preset preset) {
    core::Split s{f.p, std::vector<bool>(f.groups.size(), false)};
    switch (preset) {
    case Preset::daubechies:
        for (std::size_t g = 0; g < f.groups.size(); ++g) s.group_in_h0[g] = modulus(f.groups[g]) < 1;
        break;
    case Preset::max_phase:
        for (std::size_t g = 0; g < f.groups.size(); ++g) s.group_in_h0[g] = modulus(f.groups[g]) > 1;
        break;
    case Preset::cdf: s.zeros_h0 = std::max(2, f.p - f.p % 2); break;
    case Preset::degenerate: s.zeros_h0 = 0; break;
    }
    if (s.zeros_h0 > 2 * f.p) s.zeros_h0 = 2 * f.p;
    return s;
}

bool same_split(const core::Split& a, const core::Split& b) { return a.zeros_h0 == b.zeros_h0 && a.group_in_h0 == b.group_in_h0; }

std::shared_ptr<const DesignerData> DesignerModel::data() const {
    std::lock_guard lock{mutex_};
    const auto it = cache_.find(p_);
    return it == cache_.end() ? nullptr : it->second;
}

std::shared_ptr<const DesignerResult> DesignerModel::result() const {
    std::lock_guard lock{mutex_};
    return result_;
}

std::optional<std::uint64_t> DesignerModel::job() const {
    std::lock_guard lock{mutex_};
    return result_job_ ? result_job_ : data_job_;
}

void DesignerModel::set_p(int p, JobRunner& jobs) {
    p_ = std::clamp(p, kMinP, kMaxP);
    if (!data()) {
        if (data_job_) jobs.cancel(*data_job_);
        const int target = p_;
        const auto id = jobs.submit(fmt::format(fmt::runtime(tr("tui.designer.job.splits")), fmt::arg("p", target)),
                                    {tr("tui.designer.stage.roots"), tr("tui.designer.stage.enumerate"), tr("tui.designer.stage.classify")},
                                    [this, target](JobContext& ctx) {
                                        auto d = std::make_shared<DesignerData>();
                                        d->p = target;
                                        ctx.begin(0);
                                        d->f = core::factorise_maxflat(target);
                                        for (const auto& g : d->f.groups) {
                                            const std::complex<double> z{static_cast<double>(g.root.re), static_cast<double>(g.root.im)};
                                            d->roots.push_back(z);
                                            if (g.conjugate_pair) d->roots.push_back(std::conj(z));
                                        }
                                        ctx.finish();
                                        if (ctx.cancelled()) return;
                                        ctx.begin(1);
                                        const auto splits = core::enumerate_splits(d->f);
                                        ctx.finish();
                                        ctx.begin(2);
                                        for (std::size_t i = 0; i < splits.size(); ++i) {
                                            if (ctx.cancelled()) return;
                                            SplitRow row{splits[i], 0, 0, {}, false, false};
                                            if (auto bank = fast_bank(d->f, splits[i])) {
                                                row.h0_len = core::trim(bank->h0, 0.0).coeffs.size();
                                                row.f0_len = core::trim(bank->f0, 0.0).coeffs.size();
                                                row.info = core::classify(*bank);
                                                row.orthogonal = row.info.kind == core::SplitKind::orthogonal;
                                                row.linear_phase = core::is_linear_phase(bank->h0, 1e-9) && core::is_linear_phase(bank->f0, 1e-9);
                                            }
                                            if (row.info.wavelet) ++d->wavelet_rows;
                                            d->rows.push_back(std::move(row));
                                            ctx.report(static_cast<double>(i + 1) / static_cast<double>(splits.size()));
                                        }
                                        ctx.finish();
                                        std::lock_guard lock{mutex_};
                                        cache_[target] = std::move(d);
                                    });
        std::lock_guard lock{mutex_};
        data_job_ = id;
    }
    sync(jobs);
}

void DesignerModel::sync(JobRunner& jobs) {
    const auto d = data();
    if (!d) return;
    if (split_.group_in_h0.size() != d->f.groups.size() || split_.zeros_h0 > 2 * d->f.p || split_.zeros_h0 < 0) {
        split_ = preset_split(d->f, Preset::daubechies);
        request(jobs);
        return;
    }
    const auto r = result();
    if (!r || !same_split(r->split, split_)) {
        std::lock_guard lock{mutex_};
        if (result_job_) return;
    }
    if (!r) request(jobs);
}

void DesignerModel::set_split(core::Split s, JobRunner& jobs) {
    split_ = std::move(s);
    request(jobs);
}

void DesignerModel::request(JobRunner& jobs) {
    const auto d = data();
    if (!d) return;
    {
        std::lock_guard lock{mutex_};
        if (result_job_) jobs.cancel(*result_job_);
    }
    const auto split = split_;
    const auto id = jobs.submit(tr("tui.designer.job.result"), {tr("tui.designer.stage.bank")}, [this, d, split](JobContext& ctx) {
        ctx.begin(0);
        auto bank = core::bank_from_split(d->f, split, "p" + std::to_string(d->p));
        if (!bank) {
            ctx.fail(bank.error().message);
            return;
        }
        auto r = std::make_shared<DesignerResult>();
        r->split = split;
        r->bank = std::move(*bank);
        r->pr = core::check_pr(r->bank, 1e-9);
        r->delay = r->pr.delay;
        r->orthogonal = core::check_orthogonality(r->bank, 1e-9).passed();
        r->linear_phase = core::is_linear_phase(r->bank.h0, 1e-9) && core::is_linear_phase(r->bank.f0, 1e-9);
        r->info = core::classify(r->bank);
        for (int i = kMinIterations; i <= kMaxIterations; ++i) {
            if (auto c = core::cascade(r->bank.h0, i)) r->phi_steps.push_back(std::move(*c));
            if (auto c = core::cascade(r->bank.f0, i)) r->phi_dual_steps.push_back(std::move(*c));
        }
        if (!r->phi_steps.empty()) r->phi = r->phi_steps.back();
        if (!r->phi_dual_steps.empty()) r->phi_dual = r->phi_dual_steps.back();
        r->h0_response = core::frequency_response(r->bank.h0, 512);
        r->f0_response = core::frequency_response(r->bank.f0, 512);
        if (same_split(split, preset_split(d->f, Preset::daubechies)))
            r->title = fmt::format(fmt::runtime(tr("tui.designer.title.daubechies")), fmt::arg("p", d->p));
        else if (same_split(split, preset_split(d->f, Preset::max_phase)))
            r->title = fmt::format(fmt::runtime(tr("tui.designer.title.max_phase")), fmt::arg("p", d->p));
        else if (r->linear_phase)
            r->title = tr("tui.designer.title.linear_phase");
        else
            r->title = tr("tui.designer.title.custom");
        ctx.finish();
        std::lock_guard lock{mutex_};
        result_ = std::move(r);
        result_job_.reset();
    });
    std::lock_guard lock{mutex_};
    if (!result_ || !same_split(result_->split, split)) result_job_ = id;
}

bool DesignerModel::allowed(const SplitRow& row, const DesignerData& d) const {
    const auto& s = row.split;
    if (constraints_.linear_phase)
        for (std::size_t g = 0; g < d.f.groups.size(); ++g)
            if (s.group_in_h0[g] != s.group_in_h0[d.f.groups[g].reciprocal]) return false;
    if (constraints_.orthogonal) {
        if (s.zeros_h0 != d.f.p) return false;
        for (std::size_t g = 0; g < d.f.groups.size(); ++g)
            if (d.f.groups[g].reciprocal != g && s.group_in_h0[g] == s.group_in_h0[d.f.groups[g].reciprocal]) return false;
    }
    if (constraints_.wavelet_only && !row.info.wavelet) return false;
    return true;
}

std::vector<std::size_t> DesignerModel::visible_rows() const {
    std::vector<std::size_t> out;
    const auto d = data();
    if (!d) return out;
    for (std::size_t i = 0; i < d->rows.size(); ++i)
        if (allowed(d->rows[i], *d)) out.push_back(i);
    return out;
}

void DesignerModel::toggle_group(std::size_t g, JobRunner& jobs) {
    const auto d = data();
    if (!d || g >= split_.group_in_h0.size()) return;
    auto s = split_;
    s.group_in_h0[g] = !s.group_in_h0[g];
    const auto r = d->f.groups[g].reciprocal;
    if ((constraints_.linear_phase || constraints_.orthogonal) && r != g) s.group_in_h0[r] = !s.group_in_h0[r];
    set_split(std::move(s), jobs);
}

void DesignerModel::zeros(int delta, JobRunner& jobs) {
    if (constraints_.orthogonal) return;
    auto s = split_;
    s.zeros_h0 = std::clamp(s.zeros_h0 + delta, 0, 2 * p_);
    if (s.zeros_h0 != split_.zeros_h0) set_split(std::move(s), jobs);
}

void DesignerModel::swap(JobRunner& jobs) {
    auto s = split_;
    s.zeros_h0 = 2 * p_ - s.zeros_h0;
    for (std::size_t g = 0; g < s.group_in_h0.size(); ++g) s.group_in_h0[g] = !s.group_in_h0[g];
    set_split(std::move(s), jobs);
}

void DesignerModel::preset(Preset p, JobRunner& jobs) {
    const auto d = data();
    if (!d) return;
    set_split(preset_split(d->f, p), jobs);
}

void DesignerModel::set_constraints(Constraints c, JobRunner& jobs) {
    constraints_ = c;
    const auto d = data();
    if (!d) return;
    const auto visible = visible_rows();
    for (auto i : visible)
        if (same_split(d->rows[i].split, split_)) return;
    if (!visible.empty()) set_split(d->rows[visible.front()].split, jobs);
}

void DesignerModel::step(int direction, JobRunner& jobs) {
    const auto d = data();
    const auto visible = visible_rows();
    if (!d || visible.empty()) return;
    std::size_t at = visible.size();
    for (std::size_t i = 0; i < visible.size(); ++i)
        if (same_split(d->rows[visible[i]].split, split_)) at = i;
    std::size_t next = 0;
    if (at < visible.size()) next = (at + visible.size() + static_cast<std::size_t>(direction > 0 ? 1 : visible.size() - 1)) % visible.size();
    set_split(d->rows[visible[next]].split, jobs);
}

void DesignerModel::set_cascade_iterations(int i) { iterations_ = std::clamp(i, kMinIterations, kMaxIterations); }

}  // namespace signals::tui
