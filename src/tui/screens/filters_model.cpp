#include "tui/screens/filters_model.hpp"

#include <algorithm>
#include <cmath>

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
            if (!e.error && (!e.analysis || e.analysis->response.omega.size() != points)) todo.push_back(e.ref);
    }
    if (todo.empty()) return;
    std::vector<std::string> stages;
    for (const auto& r : todo) stages.push_back(r.label());
    jobs.submit(tr("tui.filters.job"), std::move(stages), [this, todo, points](JobContext& ctx) {
        for (std::size_t i = 0; i < todo.size(); ++i) {
            if (ctx.cancelled()) return;
            ctx.begin(i);
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
        }
    });
}

}  // namespace signals::tui
