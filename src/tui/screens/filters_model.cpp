#include "tui/screens/filters_model.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <new>
#include <numbers>

#include <fmt/format.h>

#include "core/bank.hpp"
#include "core/design/families.hpp"
#include "i18n/i18n.hpp"
#include "tui/shell/errors.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr double kLinearPhaseTol = 1e-9;
constexpr double kOrthogonalityTol = 1e-12;

std::string_view role_name(FilterRole r) {
    switch (r) {
    case FilterRole::h0: return "h₀";
    case FilterRole::h1: return "h₁";
    case FilterRole::f0: return "f₀";
    case FilterRole::f1: return "f₁";
    }
    return "";
}

const core::Laurent& role_filter(const core::FilterBank& b, FilterRole r) {
    switch (r) {
    case FilterRole::h0: return b.h0;
    case FilterRole::h1: return b.h1;
    case FilterRole::f0: return b.f0;
    case FilterRole::f1: return b.f1;
    }
    return b.h0;
}

bool analysis_side(FilterRole r) { return r == FilterRole::h0 || r == FilterRole::h1; }

}  // namespace

std::string FilterRef::label() const {
    switch (kind) {
    case Kind::bank: return std::string{role_name(role)} + " " + wavelet;
    case Kind::equivalent: return (detail ? "d" : "a") + subscript_count(level) + " " + wavelet;
    case Kind::distortion: return "T(z) " + wavelet;
    case Kind::alias: return "A(z) " + wavelet;
    }
    return wavelet;
}

LibraryNode build_library(std::string_view equivalent_wavelet, std::size_t levels) {
    LibraryNode root{tr("tui.filters.library")};
    LibraryNode wavelets{tr("tui.filters.group.wavelets")};
    for (auto name : core::wavelet_names()) {
        const auto info = core::wavelet_info(name);
        const std::string family = info ? info->family : std::string{"?"};
        auto it = std::find_if(wavelets.children.begin(), wavelets.children.end(), [&](const auto& n) { return n.label == family; });
        if (it == wavelets.children.end()) it = wavelets.children.insert(wavelets.children.end(), LibraryNode{family});
        it->children.push_back({std::string{name}, {}, FilterRef{FilterRef::Kind::bank, std::string{name}, FilterRole::h0}});
    }
    const std::string eq_name{equivalent_wavelet};
    LibraryNode equivalent{fmt::format(fmt::runtime(tr("tui.filters.group.equivalent")), fmt::arg("wavelet", eq_name), fmt::arg("levels", levels))};
    FilterRef approx{FilterRef::Kind::equivalent, eq_name, FilterRole::h0, levels, false};
    equivalent.children.push_back({approx.label(), {}, approx});
    for (std::size_t j = levels; j >= 1; --j) {
        FilterRef d{FilterRef::Kind::equivalent, eq_name, FilterRole::h0, j, true};
        equivalent.children.push_back({d.label(), {}, d});
    }
    LibraryNode chain{tr("tui.filters.group.chain")};
    for (auto kind : {FilterRef::Kind::distortion, FilterRef::Kind::alias}) {
        FilterRef r{kind, eq_name};
        chain.children.push_back({r.label(), {}, r});
    }
    root.children.push_back(std::move(wavelets));
    root.children.push_back({tr("tui.filters.group.classic"), {}, {}, "M6"});
    root.children.push_back(std::move(equivalent));
    root.children.push_back(std::move(chain));
    root.children.push_back({tr("tui.filters.group.cwt"), {}, {}, "M6"});
    root.children.push_back({tr("tui.filters.group.spectra"), {}, {}, "M6"});
    root.children.push_back({tr("tui.filters.group.mine"), {}, {}, "M6"});
    return root;
}

core::Result<FilterAnalysis> analyze(const FilterRef& ref, std::size_t points) {
    const auto bank = core::wavelet(ref.wavelet);
    if (!bank) return std::unexpected(bank.error());
    FilterAnalysis a;
    a.ref = ref;
    double lo = 0, hi = 0.5;
    switch (ref.kind) {
    case FilterRef::Kind::bank:
        a.h = role_filter(*bank, ref.role);
        if (ref.role == FilterRole::h1 || ref.role == FilterRole::f1) lo = 0.5, hi = 1.0;
        break;
    case FilterRef::Kind::equivalent: {
        auto eq = core::equivalent_filter(*bank, ref.level, ref.detail);
        if (!eq) return std::unexpected(eq.error());
        a.h = std::move(*eq);
        hi = std::ldexp(1.0, -static_cast<int>(ref.level) + (ref.detail ? 1 : 0));
        lo = ref.detail ? hi / 2 : 0.0;
        break;
    }
    case FilterRef::Kind::distortion: a.h = core::distortion_transfer(*bank), lo = -1; break;
    case FilterRef::Kind::alias: a.h = core::alias_transfer(*bank), lo = -1; break;
    }
    a.response = core::frequency_response(a.h, points);
    a.metrics = lo == 0 ? core::band_metrics(a.response, hi / 2, std::min(1.0, 1.5 * hi)) : core::band_metrics(a.response, -1.0, 2.0);
    if (auto zp = core::zeros_poles(a.h)) a.zp = std::move(*zp);
    a.linear_phase = core::is_linear_phase(a.h, kLinearPhaseTol);
    const auto pr = core::check_pr(*bank);
    a.pr_error = std::max(pr.alias.value, pr.distortion.value);
    const auto info = core::wavelet_info(ref.wavelet);
    if (info && info->orthogonal) a.orth_error = core::check_orthogonality(*bank, kOrthogonalityTol).value;
    if (const auto reg = core::wavelet_regularity(ref.wavelet)) {
        const bool synthesis = ref.kind == FilterRef::Kind::bank && !analysis_side(ref.role);
        a.sobolev = synthesis ? reg->sobolev_f0 : reg->sobolev_h0;
        if (ref.kind == FilterRef::Kind::bank && ref.role == FilterRole::h0) a.zeros_at_minus_one = reg->zeros_h0;
        if (ref.kind == FilterRef::Kind::bank && ref.role == FilterRole::f0) a.zeros_at_minus_one = reg->zeros_f0;
    }
    return a;
}

core::FrequencyResponse range_response(const core::Laurent& h, double lo, double hi, std::size_t points, const core::FrequencyResponse* full) {
    constexpr double pi = std::numbers::pi;
    core::FrequencyResponse r;
    r.omega.resize(points);
    r.h.resize(points);
    r.magnitude.resize(points);
    r.magnitude_db.resize(points);
    r.phase.resize(points);
    r.group_delay.resize(points);
    std::vector<std::complex<double>> moment(points);
    for (std::size_t k = 0; k < points; ++k) {
        const double w = pi * (points > 1 ? lo + (hi - lo) * static_cast<double>(k) / static_cast<double>(points - 1) : lo);
        r.omega[k] = w;
        std::complex<double> sum{0, 0}, m{0, 0};
        for (std::size_t j = 0; j < h.coeffs.size(); ++j) {
            const auto n = static_cast<double>(h.offset + static_cast<std::ptrdiff_t>(j));
            const auto term = h.coeffs[j] * std::polar(1.0, -w * n);
            sum += term;
            m += n * term;
        }
        r.h[k] = sum;
        moment[k] = m;
        r.magnitude[k] = std::abs(sum);
        r.magnitude_db[k] = r.magnitude[k] > 0 ? std::max(-300.0, 20 * std::log10(r.magnitude[k])) : -300.0;
        r.phase[k] = std::arg(sum);
    }
    r.phase_unwrapped = r.phase;
    for (std::size_t k = 1; k < points; ++k) {
        double d = r.phase_unwrapped[k] - r.phase_unwrapped[k - 1];
        d -= 2 * pi * std::round(d / (2 * pi));
        r.phase_unwrapped[k] = r.phase_unwrapped[k - 1] + d;
    }
    if (full && !full->omega.empty() && points > 0) {
        const auto step = full->omega.size() > 1 ? full->omega[1] - full->omega[0] : pi;
        const auto i = std::min(full->omega.size() - 1, static_cast<std::size_t>(std::max(0.0, std::round(r.omega.front() / step))));
        const double shift = 2 * pi * std::round((full->phase_unwrapped[i] - r.phase_unwrapped.front()) / (2 * pi));
        for (auto& p : r.phase_unwrapped) p += shift;
    }
    const double peak = full && !full->magnitude.empty() ? *std::max_element(full->magnitude.begin(), full->magnitude.end())
                                                         : (points ? *std::max_element(r.magnitude.begin(), r.magnitude.end()) : 0.0);
    for (std::size_t k = 0; k < points; ++k)
        r.group_delay[k] = r.magnitude[k] > 1e-12 * peak ? (moment[k] / r.h[k]).real() : std::numeric_limits<double>::quiet_NaN();
    return r;
}

std::vector<double> masked_group_delay(const core::FrequencyResponse& r, double relative, std::optional<double> full_peak) {
    const double peak = full_peak ? *full_peak : r.magnitude.empty() ? 0.0 : *std::max_element(r.magnitude.begin(), r.magnitude.end());
    std::vector<double> out(r.group_delay);
    for (std::size_t k = 0; k < out.size(); ++k)
        if (r.magnitude[k] < relative * peak) out[k] = std::numeric_limits<double>::quiet_NaN();
    return out;
}

core::Result<void> ComparisonSet::add(FilterRef ref) {
    std::lock_guard lock{mutex_};
    if (std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.ref == ref; })) return {};
    if (entries_.size() >= kCapacity)
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.filters.full")), fmt::arg("count", kCapacity)));
    entries_.push_back({std::move(ref)});
    return {};
}

void ComparisonSet::remove(std::size_t i) {
    std::lock_guard lock{mutex_};
    if (i >= entries_.size()) return;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    if (reference_) {
        if (*reference_ == i) reference_.reset();
        else if (*reference_ > i) --*reference_;
    }
    if (focus_ > i || focus_ >= entries_.size()) focus_ = focus_ > 0 ? focus_ - 1 : 0;
}

void ComparisonSet::toggle(std::size_t i) {
    std::lock_guard lock{mutex_};
    if (i < entries_.size()) entries_[i].visible = !entries_[i].visible;
}

void ComparisonSet::set_reference(std::optional<std::size_t> i) {
    std::lock_guard lock{mutex_};
    reference_ = i && *i < entries_.size() ? i : std::nullopt;
}

void ComparisonSet::set_focus(std::size_t i) {
    std::lock_guard lock{mutex_};
    if (i < entries_.size()) focus_ = i;
}

std::vector<ComparisonSet::Entry> ComparisonSet::entries() const {
    std::lock_guard lock{mutex_};
    return entries_;
}

std::optional<std::size_t> ComparisonSet::reference() const {
    std::lock_guard lock{mutex_};
    return reference_;
}

std::size_t ComparisonSet::focus() const {
    std::lock_guard lock{mutex_};
    return focus_;
}

void ComparisonSet::store(const FilterRef& ref, std::shared_ptr<const FilterAnalysis> analysis, std::optional<std::string> error) {
    std::lock_guard lock{mutex_};
    std::erase(pending_, ref);
    for (auto& e : entries_)
        if (e.ref == ref) {
            e.analysis = std::move(analysis);
            e.error = std::move(error);
        }
}

void ComparisonSet::request(JobRunner& jobs, std::size_t points) {
    std::vector<FilterRef> todo;
    {
        std::lock_guard lock{mutex_};
        for (const auto& e : entries_)
            if (!e.error && (!e.analysis || e.analysis->response.omega.size() != points) &&
                std::find(pending_.begin(), pending_.end(), e.ref) == pending_.end())
                todo.push_back(e.ref);
        pending_.insert(pending_.end(), todo.begin(), todo.end());
    }
    if (todo.empty()) return;
    std::vector<std::string> stages;
    for (const auto& r : todo) stages.push_back(r.label());
    jobs.submit(tr("tui.filters.job"), std::move(stages), [this, todo, points](JobContext& ctx) {
        for (std::size_t i = 0; i < todo.size(); ++i) {
            if (ctx.cancelled()) {
                std::lock_guard lock{mutex_};
                for (std::size_t k = i; k < todo.size(); ++k) std::erase(pending_, todo[k]);
                return;
            }
            ctx.begin(i);
            try {
                auto a = analyze(todo[i], points);
                if (!a) {
                    const auto text = error_text(a.error());
                    store(todo[i], nullptr, text);
                    ctx.finish(0, text);
                    continue;
                }
                const auto bytes = a->h.coeffs.size() * sizeof(double) + a->response.omega.size() * 7 * sizeof(double);
                store(todo[i], std::make_shared<const FilterAnalysis>(std::move(*a)), std::nullopt);
                ctx.finish(bytes);
            } catch (const std::bad_alloc&) {
                store(todo[i], nullptr, tr("tui.jobs.out_of_memory"));
                ctx.finish(0, tr("tui.jobs.out_of_memory"));
            } catch (const std::exception& e) {
                const auto text = fmt::format(fmt::runtime(tr("tui.jobs.internal_error")), fmt::arg("what", e.what()));
                store(todo[i], nullptr, text);
                ctx.finish(0, text);
            }
        }
    });
}

void ComparisonSet::request_view(JobRunner& jobs, double lo, double hi, std::size_t points) {
    std::vector<std::pair<FilterRef, std::shared_ptr<const FilterAnalysis>>> todo;
    {
        std::lock_guard lock{mutex_};
        if (view_pending_) return;
        for (const auto& e : entries_)
            if (e.analysis && (!e.view || e.view_lo != lo || e.view_hi != hi || e.view->omega.size() != points))
                todo.emplace_back(e.ref, e.analysis);
        if (todo.empty()) return;
        view_pending_ = true;
    }
    jobs.submit(tr("tui.filters.view_job"), {tr("tui.filters.view_stage")}, [this, todo, lo, hi, points](JobContext& ctx) {
        struct ClearPending {
            ComparisonSet& set;
            ~ClearPending() {
                std::lock_guard lock{set.mutex_};
                set.view_pending_ = false;
            }
        } clear{*this};
        ctx.begin(0);
        for (std::size_t i = 0; i < todo.size() && !ctx.cancelled(); ++i) {
            auto r = std::make_shared<const core::FrequencyResponse>(range_response(todo[i].second->h, lo, hi, points, &todo[i].second->response));
            std::lock_guard lock{mutex_};
            for (auto& e : entries_)
                if (e.ref == todo[i].first) e.view = r, e.view_lo = lo, e.view_hi = hi;
            ctx.report(static_cast<double>(i + 1) / static_cast<double>(todo.size()));
        }
        ctx.finish();
    });
}

std::optional<std::string> ComparisonSet::pending_label() const {
    std::lock_guard lock{mutex_};
    if (pending_.empty()) return std::nullopt;
    return pending_.front().label();
}

bool ComparisonSet::view_pending() const {
    std::lock_guard lock{mutex_};
    return view_pending_;
}

}  // namespace signals::tui
