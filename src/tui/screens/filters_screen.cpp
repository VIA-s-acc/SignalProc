#include "tui/screens/filters_screen.hpp"

#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

#include <fmt/format.h>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "i18n/i18n.hpp"
#include "core/design/families.hpp"
#include "core/bank.hpp"
#include "tui/screens/publish_util.hpp"
#include "tui/shell/palette.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/text_util.hpp"
#include "tui/widgets/zplane.hpp"

namespace signals::tui {
namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kMinSpan = 16.0 / FiltersScreen::kPoints;
constexpr double kLogFloor = 1e-3;
constexpr double kWheelStep = 1.25;
constexpr double kHalfPowerDb = -3.0102999566398120;
constexpr double kMaskRelative = 1e-3;
constexpr double kCheckTol = 1e-10;
constexpr double kRoundOff = 1e-15;
constexpr int kMinLeft = 26;
constexpr int kComparisonRows = 8;
constexpr int kRowPanels = 9;
const ftxui::Box kNoBox{1, 0, 1, 0};

bool valid(const ftxui::Box& b) { return b.x_max >= b.x_min && b.y_max >= b.y_min; }

std::string pi_text(double w, int decimals = 3) { return std::isfinite(w) ? format_fixed(w, decimals) + "π" : "—"; }

std::string signed_text(double v, int decimals) {
    if (!std::isfinite(v)) return "—";
    return (v > 0 ? "+" : "") + format_fixed(v, decimals);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string describe(const FilterRef& r) {
    switch (r.kind) {
    case FilterRef::Kind::bank:
    case FilterRef::Kind::user:
        switch (r.role) {
        case FilterRole::h0: return tr("tui.filters.role.h0");
        case FilterRole::h1: return tr("tui.filters.role.h1");
        case FilterRole::f0: return tr("tui.filters.role.f0");
        case FilterRole::f1: return tr("tui.filters.role.f1");
        }
        break;
    case FilterRef::Kind::equivalent: return tr("tui.filters.role.equivalent");
    case FilterRef::Kind::distortion: return tr("tui.filters.role.distortion");
    case FilterRef::Kind::alias: return tr("tui.filters.role.alias");
    case FilterRef::Kind::classical: return tr("tui.filters.role.classical");
    case FilterRef::Kind::scaling: return tr("tui.filters.role.scaling");
    case FilterRef::Kind::wavelet_fn: return tr("tui.filters.role.wavelet_fn");
    case FilterRef::Kind::cwt: return tr("tui.filters.role.cwt");
    case FilterRef::Kind::packet: return tr("tui.filters.role.packet");
    }
    return {};
}

std::string role_label(FilterRole r) {
    switch (r) {
    case FilterRole::h0: return "h₀";
    case FilterRole::h1: return "h₁";
    case FilterRole::f0: return "f₀";
    case FilterRole::f1: return "f₁";
    }
    return {};
}

bool chain(const FilterRef& r) { return r.kind == FilterRef::Kind::distortion || r.kind == FilterRef::Kind::alias; }

struct Root {
    std::complex<double> z;
    int count = 1;
};

std::vector<Root> group_roots(const std::vector<std::complex<double>>& zeros) {
    std::vector<Root> out;
    for (auto z : zeros) {
        if (std::abs(z.imag()) < 1e-6) z = {z.real(), 0.0};
        else if (z.imag() < 0) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](const Root& r) { return std::abs(r.z - z) < 1e-4; });
        if (it != out.end()) ++it->count;
        else out.push_back({z});
    }
    return out;
}

std::string root_text(const Root& r) {
    std::string s = r.z.imag() == 0 ? format_number(r.z.real(), 3)
                                    : format_number(r.z.real(), 3) + "±" + format_number(std::abs(r.z.imag()), 3) + "j";
    return r.count > 1 ? s + " ×" + std::to_string(r.count) : s;
}

}  // namespace

FiltersScreen::FiltersScreen(UiContext& ui, std::shared_ptr<ComparisonSet> set)
    : ui_{ui}, set_{std::move(set)}, library_{build_library()}, layout_{{{"left", {33, 30, 33, 36, 42}}}} {
    panel_boxes_.fill(kNoBox);
    title_boxes_.fill(kNoBox);
    plot_boxes_.fill(kNoBox);
}

std::string FiltersScreen::tab_label() const { return tr("tui.tab.filters"); }

std::vector<Hint> FiltersScreen::hints() const {
    return {{"Tab", tr("tui.signal.hint.tab")},       {"z", tr("tui.signal.hint.zoom")},
            {"/", tr("tui.filters.hint.search")},     {"Enter", tr("tui.filters.hint.add")},
            {"Space", tr("tui.filters.hint.visible")}, {"r", tr("tui.filters.hint.reference")},
            {"Del", tr("tui.filters.hint.remove")},    {"←→", tr("tui.filters.hint.cursor")},
            {"b", tr("tui.signal.hint.cursor_b")},     {"+ − 0 < >", tr("tui.hint.plot_zoom")},
            {"[ ]", tr("tui.filters.hint.vertical")},  {"↑↓", tr("tui.filters.hint.focus_row")},
            {"l", tr("tui.filters.hint.log")},         {"d", tr("tui.filters.hint.db")},
            {"n", tr("tui.filters.hint.normalise")},   {"t", tr("tui.filters.hint.role")},
            {"o/O", tr("tui.filters.hint.order")},      {"f/F", tr("tui.filters.hint.edge")},
            {"F1", tr("tui.filters.hint.f1")},         {tr("tui.signal.key.wheel"), tr("tui.filters.hint.zoom")},
            {tr("tui.signal.key.click"), tr("tui.signal.hint.click")}};
}

std::string FiltersScreen::context() const { return tr("tui.filters.context"); }

std::optional<TheoryLink> FiltersScreen::theory_link() const {
    const auto* e = focused();
    const bool equivalent = e && (e->ref.kind == FilterRef::Kind::equivalent || e->ref.kind == FilterRef::Kind::packet);
    if (e && (e->ref.kind == FilterRef::Kind::scaling || e->ref.kind == FilterRef::Kind::wavelet_fn || e->ref.kind == FilterRef::Kind::cwt))
        return TheoryLink{TheoryLink::Kind::card, "phi_psi"};
    if (e && e->ref.kind == FilterRef::Kind::classical) {
        const auto spec = parse_classical(e->ref.wavelet);
        return TheoryLink{TheoryLink::Kind::card, spec && spec->method != ClassicalSpec::Method::iir ? "fir_design" : "iir_design"};
    }
    return TheoryLink{TheoryLink::Kind::card, equivalent ? "equivalent_filters" : "freq_response"};
}

void FiltersScreen::publish(theory::LiveValues& v) const {
    const auto* e = focused();
    v.source = tab_label() + " ▸ " + (e ? e->ref.label() : std::string{"—"});
    if (!e || !e->analysis) return;
    const auto& a = *e->analysis;
    const auto pi = [](double w) { return format_fixed(w, 3) + "π"; };
    v.set("block.filter", e->ref.label());
    v.set("block.length", std::to_string(a.h.coeffs.size()));
    if (const auto spec = e->ref.kind == FilterRef::Kind::classical ? parse_classical(e->ref.wavelet) : std::nullopt) {
        v.set("block.order", std::to_string(spec->order));
        std::string cutoff;
        for (const double w : spec->edges) cutoff += (cutoff.empty() ? "" : "–") + pi(w);
        v.set("block.cutoff", cutoff);
        if (a.zpk) v.set("metric.poles", std::to_string(a.zpk->p.size()));
        if (a.stable) v.set("check.stable", yes_no(*a.stable));
    }
    v.set("block.points", std::to_string(a.response.omega.size()));
    v.set("block.zeros_at_minus_one", a.zeros_at_minus_one ? std::to_string(*a.zeros_at_minus_one) : std::string{"—"});
    v.set("block.linear_phase", yes_no(a.linear_phase));
    v.set("metric.cutoff_3db", pi(a.metrics.cutoff_3db));
    v.set("metric.ripple_db", format_fixed(a.metrics.passband_ripple_db, 2) + " " + tr("tui.unit.db"));
    v.set("metric.attenuation_db", format_fixed(a.metrics.stopband_attenuation_db, 1) + " " + tr("tui.unit.db"));
    v.set("metric.transition", pi(a.metrics.transition_width));
    v.set("block.bank", e->ref.wavelet);
    if (a.factors) v.set("block.factors", std::to_string(*a.factors));
    if (a.zeros_at_minus_one) v.set("metric.zeros", std::to_string(*a.zeros_at_minus_one));
    if (a.partition_error) v.set("metric.partition", format_number(*a.partition_error, 2));
    v.set("block.band", std::string{e->ref.detail ? "d" : "a"} + subscript_number(e->ref.level));
    v.set("block.level", std::to_string(e->ref.level));
    if (const auto bank = core::wavelet(e->ref.wavelet)) {
        const auto pr = core::check_pr(*bank, 1e-9);
        v.set_check(renamed(pr.alias, "check.pr_alias"));
        v.set_check(renamed(pr.distortion, "check.pr_distortion"));
    }
}

std::string FiltersScreen::key_of(Panel p) {
    static constexpr std::array<std::string_view, panel_count> keys{"magnitude", "phase",    "delay",   "impulse",   "zeros",
                                                                    "properties", "measures", "library", "comparison"};
    return std::string{keys[static_cast<std::size_t>(p)]};
}

FiltersScreen::Panel FiltersScreen::panel_of(const std::string& key) {
    for (int p = 0; p < panel_count; ++p)
        if (key_of(static_cast<Panel>(p)) == key) return static_cast<Panel>(p);
    return magnitude;
}

Axis FiltersScreen::omega_axis() const {
    if (log_) return {std::max(lo_, kLogFloor), hi_, AxisScale::log10};
    if (hi_ - lo_ >= 0.5 - 1e-12) return {lo_ * kPi, hi_ * kPi, AxisScale::pi};
    return {lo_, hi_, AxisScale::linear};
}

double FiltersScreen::to_axis(double w) const { return omega_axis().scale == AxisScale::pi ? w * kPi : w; }

double FiltersScreen::from_axis(double v) const { return omega_axis().scale == AxisScale::pi ? v / kPi : v; }

ftxui::Box FiltersScreen::plot_box(int plot) const {
    if (plot < 0 || plot > 2 || !valid(plot_boxes_[static_cast<std::size_t>(plot)])) return kNoBox;
    PlotSpec s;
    return plot_area(plot_boxes_[static_cast<std::size_t>(plot)], s);
}

int FiltersScreen::plot_under(int x, int y) const {
    for (int p = 0; p < 3; ++p)
        if (plot_box(p).Contain(x, y)) return p;
    return -1;
}

double FiltersScreen::omega_at(std::string_view plot, int x) const {
    const int p = plot == "phase" ? 1 : plot == "delay" ? 2 : 0;
    const auto a = plot_box(p);
    if (!valid(a)) return lo_;
    return std::clamp(from_axis(value_at_column(omega_axis(), x - a.x_min, a.x_max - a.x_min + 1)), 0.0, span_);
}

ftxui::Box FiltersScreen::area(std::string_view id) const {
    if (id == "magnitude_plot") return plot_box(0);
    if (id == "phase_plot") return plot_box(1);
    if (id == "delay_plot") return plot_box(2);
    return panel_boxes_[static_cast<std::size_t>(panel_of(std::string{id}))];
}

const ComparisonSet::Entry* FiltersScreen::focused() const {
    const auto f = set_->focus();
    return f < entries_.size() ? &entries_[f] : nullptr;
}

Rgb FiltersScreen::color_of(std::size_t index) const {
    const auto& th = ui_.theme;
    if (set_->reference() == index) return th.fg;
    std::string primary;
    for (const auto& e : entries_)
        if (e.ref.kind == FilterRef::Kind::bank || e.ref.kind == FilterRef::Kind::user) {
            primary = e.ref.wavelet;
            break;
        }
    auto is_primary = [&](std::size_t i) {
        const auto& r = entries_[i].ref;
        return (r.kind == FilterRef::Kind::bank || r.kind == FilterRef::Kind::user) && r.wavelet == primary;
    };
    const auto& r = entries_[index].ref;
    if (is_primary(index)) return r.role == FilterRole::h0 || r.role == FilterRole::f0 ? th.trace[0] : th.trace[1];
    std::size_t k = 0;
    for (std::size_t j = 0; j < index; ++j)
        if (set_->reference() != j && !is_primary(j)) ++k;
    return th.trace[2 + k % 6];
}

LineStyle FiltersScreen::style_of(std::size_t index) const {
    if (set_->reference() == index) return LineStyle::dotted;
    const auto& r = entries_[index].ref;
    if ((r.kind == FilterRef::Kind::bank || r.kind == FilterRef::Kind::user) && (r.role == FilterRole::f0 || r.role == FilterRole::f1))
        return LineStyle::dashed;
    return LineStyle::solid;
}

std::string FiltersScreen::legend_mark(std::size_t index) const {
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    switch (style_of(index)) {
    case LineStyle::solid: return ascii ? "--" : "━━";
    case LineStyle::dashed: return ascii ? "- " : "╍╍";
    case LineStyle::dotted: return ascii ? ". " : "┄┄";
    }
    return "  ";
}

std::vector<FiltersScreen::Trace> FiltersScreen::traces() const {
    std::vector<Trace> out;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!e.visible || !e.analysis) continue;
        const auto& full = e.analysis->response;
        if (full.magnitude.empty()) continue;
        const bool covers = e.view && e.view_lo <= lo_ + 1e-12 && e.view_hi >= hi_ - 1e-12 && (lo_ > 0 || hi_ < span_);
        out.push_back({&e, color_of(i), style_of(i), covers ? e.view.get() : &full,
                       *std::max_element(full.magnitude_db.begin(), full.magnitude_db.end()),
                       *std::max_element(full.magnitude.begin(), full.magnitude.end()), normalise_ && !chain(e.ref)});
    }
    return out;
}

std::optional<std::size_t> FiltersScreen::index_at(const core::FrequencyResponse& r, double w) const {
    if (r.omega.empty()) return std::nullopt;
    const double target = w * kPi;
    if (target < r.omega.front() - 1e-12 || target > r.omega.back() + (r.omega.size() > 1 ? r.omega[1] - r.omega[0] : 0)) return std::nullopt;
    auto it = std::lower_bound(r.omega.begin(), r.omega.end(), target);
    std::size_t k = static_cast<std::size_t>(it - r.omega.begin());
    if (k >= r.omega.size()) k = r.omega.size() - 1;
    if (k > 0 && std::abs(r.omega[k - 1] - target) < std::abs(r.omega[k] - target)) --k;
    return k;
}

std::optional<double> FiltersScreen::value_at(const Trace& t, double w) const {
    const auto k = index_at(*t.response, w);
    if (!k) return std::nullopt;
    const auto& r = *t.response;
    if (decibels_) return r.magnitude_db[*k] - (t.normalised ? t.peak_db : 0.0);
    return r.magnitude[*k] / (t.normalised && t.peak > 0 ? t.peak : 1.0);
}

ftxui::Element FiltersScreen::cursor_line(const std::vector<Trace>& ts, const std::function<double(const Trace&, std::size_t)>& value,
                                          const std::string& unit) const {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!cursor_a_) return text("");
    bool same = true;
    for (const auto& t : ts) same = same && t.entry->ref.wavelet == ts.front().entry->ref.wavelet;
    Elements parts{text("A: ") | fg(th.accent, ui_)};
    for (const auto& t : ts) {
        auto label = t.entry->ref.label();
        if (same) label = label.substr(0, label.find(' '));
        const auto k = index_at(*t.response, *cursor_a_);
        const double v = k ? value(t, *k) : std::numeric_limits<double>::quiet_NaN();
        parts.push_back(text(label + " ") | fg(t.color, ui_));
        parts.push_back(text(format_fixed(v, 2) + (std::isfinite(v) ? unit : std::string{}) + "  ") | fg(th.fg, ui_));
    }
    return hbox(std::move(parts));
}

PlotSpec FiltersScreen::frequency_spec() const {
    PlotSpec spec;
    spec.x = omega_axis();
    if (cursor_a_) spec.cursor_a = to_axis(*cursor_a_);
    if (cursor_b_) spec.cursor_b = to_axis(*cursor_b_);
    return spec;
}

ftxui::Element FiltersScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    if (breakpoint_ <= Breakpoint::small && !layout_.maximized())
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1),
                                     fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_, &title_boxes_[static_cast<std::size_t>(p)]) |
           ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element FiltersScreen::clickable(ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({kNoBox, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element FiltersScreen::chip_rows(std::vector<Item> items, int width, int indent) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements rows, row;
    int used = indent;
    if (indent) row.push_back(text(std::string(static_cast<std::size_t>(indent), ' ')));
    for (auto& item : items) {
        const int w = display_width(item.label) + 1;
        if (used + w > width && used > indent) {
            rows.push_back(hbox(std::move(row)));
            row.clear();
            used = indent;
            if (indent) row.push_back(text(std::string(static_cast<std::size_t>(indent), ' ')));
        }
        auto t = text(item.label);
        t = item.on ? t | bold | fg(th.bg, ui_) | bg(th.trace[0], ui_) : t | fg(th.fg, ui_);
        row.push_back(clickable(t, std::move(item.action)));
        row.push_back(text(" "));
        used += w;
    }
    if (!row.empty()) rows.push_back(hbox(std::move(row)));
    return vbox(std::move(rows));
}

std::vector<const LibraryNode*> FiltersScreen::matches() const {
    std::vector<const LibraryNode*> leaves;
    std::function<void(const LibraryNode&)> walk = [&](const LibraryNode& n) {
        if (n.ref) leaves.push_back(&n);
        for (const auto& c : n.children) walk(c);
    };
    walk(library_);
    const auto q = lower(query_);
    std::vector<std::pair<int, const LibraryNode*>> ranked;
    for (const auto* n : leaves) {
        const auto label = lower(n->label);
        const auto at = label.find(q);
        if (at == std::string::npos) continue;
        ranked.emplace_back(label == q ? 0 : at == 0 ? 1 : 2, n);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<const LibraryNode*> out;
    for (const auto& [rank, n] : ranked) out.push_back(n);
    return out;
}

std::string FiltersScreen::focus_wavelet() const {
    auto bank_like = [](const FilterRef& r) { return r.kind != FilterRef::Kind::classical && r.kind != FilterRef::Kind::cwt && !r.wavelet.empty(); };
    const auto entries = set_->entries();
    const auto f = set_->focus();
    if (f < entries.size() && bank_like(entries[f].ref)) return entries[f].ref.wavelet;
    for (const auto& e : entries)
        if (bank_like(e.ref)) return e.ref.wavelet;
    return "db2";
}

void FiltersScreen::add(const FilterRef& leaf) {
    FilterRef ref = leaf;
    if (ref.kind == FilterRef::Kind::bank) ref.role = add_role_;
    notice_.clear();
    if (auto r = set_->add(ref); !r) {
        notice_ = r.error().message;
        return;
    }
    const auto entries = set_->entries();
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].ref == ref) set_->set_focus(i);
    if (ui_.jobs) set_->request(*ui_.jobs, kPoints);
}

ftxui::Element FiltersScreen::library_panel(int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const int inner = std::max(10, width - 4);
    Elements rows;
    std::size_t total = 0;
    std::function<void(const LibraryNode&)> count = [&](const LibraryNode& n) {
        total += n.ref ? 1 : 0;
        for (const auto& c : n.children) count(c);
    };
    count(library_);
    if (searching_) {
        const auto m = matches();
        rows.push_back(hbox({text("/ " + query_) | fg(th.fg, ui_), text(ui_.caps.glyphs == GlyphMode::ascii ? "_" : "▏") | fg(th.accent, ui_), filler(),
                             text(fmt::format(fmt::runtime(tr("tui.filters.found")), fmt::arg("count", m.size()), fmt::arg("total", total))) |
                                 fg(th.dim, ui_)}));
    } else {
        rows.push_back(hbox({text(tr("tui.filters.search_hint")) | fg(th.dim, ui_), filler(), text(std::to_string(total)) | fg(th.dim, ui_)}));
    }
    std::vector<Item> roles;
    for (auto r : {FilterRole::h0, FilterRole::h1, FilterRole::f0, FilterRole::f1})
        roles.push_back({role_label(r), [this, r] { add_role_ = r; }, add_role_ == r});
    rows.push_back(hbox({text(tr("tui.filters.role") + " ") | fg(th.dim, ui_), chip_rows(std::move(roles), inner, 0), text("(t)") | fg(th.dim, ui_)}));
    rows.push_back(text(""));
    auto in_set = [&](const FilterRef& r) {
        for (const auto& e : entries_) {
            if (r.kind == FilterRef::Kind::bank ? e.ref.kind == r.kind && e.ref.wavelet == r.wavelet : e.ref == r) return true;
        }
        return false;
    };
    const auto open = std::string{ui_.caps.glyphs == GlyphMode::ascii ? "v" : "▾"};
    const auto closed = std::string{ui_.caps.glyphs == GlyphMode::ascii ? ">" : "▸"};
    if (searching_) {
        const auto m = matches();
        std::vector<Item> items;
        for (std::size_t i = 0; i < m.size(); ++i) items.push_back({m[i]->label, [this, n = m[i]] { add(*n->ref); searching_ = false; query_.clear(); }, i == match_index_});
        if (items.empty()) rows.push_back(text(tr("tui.filters.nothing")) | fg(th.dim, ui_));
        else rows.push_back(chip_rows(std::move(items), inner, 0));
    } else {
        for (const auto& group : library_.children) {
            if (!group.later.empty()) {
                rows.push_back(text(closed + " " + group.label + " · " + group.later) | fg(th.dim, ui_));
                continue;
            }
            const bool collapsed = (std::find(collapsed_.begin(), collapsed_.end(), group.label) != collapsed_.end()) != group.folded;
            rows.push_back(clickable(text((collapsed ? closed : open) + " " + group.label) | fg(th.fg, ui_), [this, label = group.label] {
                auto it = std::find(collapsed_.begin(), collapsed_.end(), label);
                if (it == collapsed_.end()) collapsed_.push_back(label);
                else collapsed_.erase(it);
            }));
            if (collapsed) continue;
            const bool nested = !group.children.empty() && !group.children.front().children.empty();
            if (nested) {
                std::vector<Item> families;
                const LibraryNode* chosen = &group.children.front();
                for (const auto& f : group.children) {
                    if (f.label == family_) chosen = &f;
                    families.push_back({f.label, [this, label = f.label] { family_ = label; }, f.label == family_});
                }
                rows.push_back(chip_rows(std::move(families), inner, 2));
                std::vector<Item> names;
                for (const auto& n : chosen->children) names.push_back({n.label, [this, ref = *n.ref] { add(ref); }, in_set(*n.ref)});
                rows.push_back(chip_rows(std::move(names), inner, 4));
            } else {
                std::vector<Item> leaves;
                for (const auto& n : group.children)
                    if (n.ref) leaves.push_back({n.label, [this, ref = *n.ref] { add(ref); }, in_set(*n.ref)});
                rows.push_back(chip_rows(std::move(leaves), inner, 2));
            }
        }
    }
    rows.push_back(filler());
    rows.push_back(text(tr("tui.filters.mouse_hint.wheel")) | fg(th.dim, ui_));
    rows.push_back(text(tr("tui.filters.mouse_hint.drag")) | fg(th.dim, ui_));
    return frame_panel(library, vbox(std::move(rows)), {}, tr("tui.filters.library"));
}

ftxui::Element FiltersScreen::comparison_panel(int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements rows;
    const auto focus = set_->focus();
    const auto reference = set_->reference();
    row_boxes_.assign(entries_.size(), kNoBox);
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        const auto color = color_of(i);
        const std::string eye = e.visible ? (ascii ? "(o)" : "◉") : (ascii ? "( )" : "○");
        const auto reference_mark = reference == i ? tr("tui.filters.reference_mark") + " " : std::string{};
        const int room = std::max(4, width - 5 - display_width(legend_mark(i) + " " + reference_mark + eye) - (e.error ? 2 : 0));
        auto name = text(ellipsize((i == focus ? (ascii ? "> " : "▸ ") : "  ") + e.ref.label() + " (" + describe(e.ref) + ")", room, ascii));
        name = i == focus ? name | bold | fg(th.fg, ui_) : name | fg(e.visible ? th.fg : th.dim, ui_);
        Elements line{text(legend_mark(i) + " ") | fg(color, ui_), clickable(name, [this, i] { set_->set_focus(i); }), filler()};
        if (!reference_mark.empty()) line.push_back(text(reference_mark) | fg(th.accent, ui_));
        if (e.error) line.push_back(text(std::string{glyph(Glyph::cross, ui_.caps.glyphs)} + " ") | fg(th.err, ui_));
        line.push_back(clickable(text(eye) | fg(e.visible ? th.fg : th.dim, ui_), [this, i] { set_->toggle(i); }));
        rows.push_back(hbox(std::move(line)) | reflect(row_boxes_[i]));
    }
    if (entries_.empty()) rows.push_back(text(tr("tui.filters.empty_set")) | fg(th.dim, ui_));
    if (!notice_.empty()) rows.push_back(text(notice_) | fg(th.warn, ui_));
    rows.push_back(filler());
    
    return frame_panel(comparison, vbox(std::move(rows)), {},
                       fmt::format(fmt::runtime(tr("tui.filters.comparison")), fmt::arg("count", entries_.size()),
                                   fmt::arg("capacity", ComparisonSet::kCapacity)));
}

ftxui::Element FiltersScreen::magnitude_panel(int) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    std::vector<Chip> chips{{tr("tui.filters.chip.db"), decibels_}, {tr("tui.filters.chip.linear"), !decibels_},
                            {tr("tui.filters.chip.log"), log_}, {tr("tui.filters.chip.normalised"), normalise_}};
    if (auto p = set_->pending_label())
        chips.insert(chips.begin(), Chip{fmt::format(fmt::runtime(tr("tui.filters.computing")), fmt::arg("name", *p)), true});
    const auto title = tr("tui.filters.magnitude") + (normalise_ ? " · " + tr("tui.filters.normalised_title") : "");
    const auto ts = traces();
    if (ts.empty())
        return frame_panel(magnitude, vbox({filler(), text(tr("tui.filters.empty_plot")) | fg(th.dim, ui_) | hcenter, filler()}), std::move(chips), title);
    auto spec = frequency_spec();
    double top = decibels_ ? 3.0 : 1.1;
    if (!normalise_ || std::any_of(ts.begin(), ts.end(), [](const Trace& t) { return !t.normalised; }))
        for (const auto& t : ts) top = std::max(top, decibels_ ? t.peak_db + 3 : t.peak * 1.1);
    spec.y = decibels_ ? Axis{db_floor_, top} : Axis{0, top * linear_zoom_};
    if (normalise_) spec.y_marks = {decibels_ ? kHalfPowerDb : std::sqrt(0.5)};
    const double step = (hi_ - lo_) / 64;
    for (const auto& t : ts) {
        tui::Trace tr_{};
        tr_.color = t.color;
        tr_.style = t.style;
        tr_.focused = t.entry == focused();
        const auto& r = *t.response;
        for (std::size_t k = 0; k < r.omega.size(); ++k) {
            const double w = r.omega[k] / kPi;
            if (w < lo_ - step || w > hi_ + step || (log_ && w < kLogFloor)) continue;
            tr_.x.push_back(to_axis(w));
            tr_.y.push_back(decibels_ ? std::max(db_floor_ - 1, r.magnitude_db[k] - (t.normalised ? t.peak_db : 0.0))
                                      : r.magnitude[k] / (t.normalised && t.peak > 0 ? t.peak : 1.0));
        }
        spec.traces.push_back(std::move(tr_));
    }
    auto chart = plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | reflect(plot_boxes_[0]);
    const std::string unit = decibels_ ? " " + tr("tui.filters.db") : "";
    auto readout = [&](const char* name, const std::optional<double>& at, const std::optional<double>& base) {
        std::vector<Element> parts{text(std::string{name} + " ") | fg(th.accent, ui_)};
        if (!at) {
            parts.push_back(text("—") | fg(th.fg, ui_));
            return hbox(std::move(parts));
        }
        parts.push_back(text("ω = " + pi_text(*at) + "  ") | fg(th.fg, ui_));
        for (const auto& t : ts) {
            const auto v = value_at(t, *at);
            parts.push_back(text(t.entry->ref.label() + " ") | fg(t.color, ui_));
            parts.push_back(text((v ? format_fixed(*v, decibels_ ? 2 : 3) : std::string{"—"}) + "  ") | fg(th.fg, ui_));
        }
        if (base) {
            parts.push_back(text("Δ ") | fg(th.dim, ui_));
            for (const auto& t : ts) {
                const auto a = value_at(t, *base), b = value_at(t, *at);
                parts.push_back(text((a && b ? signed_text(*b - *a, decibels_ ? 2 : 3) : std::string{"—"}) + " ") | fg(t.color, ui_));
            }
        }
        parts.push_back(text(unit) | fg(th.dim, ui_));
        return hbox(std::move(parts));
    };
    return frame_panel(magnitude, vbox({chart | flex, readout("A", cursor_a_, std::nullopt), readout("B", cursor_b_, cursor_a_)}), std::move(chips),
                       title);
}

ftxui::Element FiltersScreen::phase_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto ts = traces();
    auto spec = frequency_spec();
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    const double step = (hi_ - lo_) / 64;
    for (const auto& t : ts) {
        tui::Trace tr_{};
        tr_.color = t.color;
        tr_.style = t.style;
        tr_.focused = t.entry == focused();
        const auto& r = *t.response;
        for (std::size_t k = 0; k < r.omega.size(); ++k) {
            const double w = r.omega[k] / kPi;
            if (w < lo_ - step || w > hi_ + step || (log_ && w < kLogFloor)) continue;
            const double v = r.magnitude[k] < kMaskRelative * t.peak ? std::numeric_limits<double>::quiet_NaN() : r.phase_unwrapped[k] / kPi;
            tr_.x.push_back(to_axis(w));
            tr_.y.push_back(v);
            if (std::isfinite(v) && w >= lo_ && w <= hi_) lo = std::min(lo, v), hi = std::max(hi, v);
        }
        spec.traces.push_back(std::move(tr_));
    }
    if (!std::isfinite(lo)) lo = -1, hi = 1;
    if (hi - lo < 0.1) lo -= 0.05, hi += 0.05;
    const double mid = (lo + hi) / 2, half = (hi - lo) / 2 * 1.08 * phase_zoom_;
    spec.y = {mid - half, mid + half};
    auto readout = cursor_line(
        ts,
        [](const Trace& t, std::size_t k) {
            const auto& r = *t.response;
            return r.magnitude[k] < kMaskRelative * t.peak ? std::numeric_limits<double>::quiet_NaN() : r.phase_unwrapped[k] / kPi;
        },
        "π");
    return frame_panel(phase, vbox({plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | reflect(plot_boxes_[1]) | flex, readout}), {},
                       tr("tui.filters.phase"));
}

ftxui::Element FiltersScreen::delay_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto ts = traces();
    auto spec = frequency_spec();
    double lo = 0, hi = -std::numeric_limits<double>::infinity();
    const double step = (hi_ - lo_) / 64;
    for (const auto& t : ts) {
        tui::Trace tr_{};
        tr_.color = t.color;
        tr_.style = t.style;
        tr_.focused = t.entry == focused();
        const auto& r = *t.response;
        const auto gd = masked_group_delay(r, kMaskRelative, t.peak);
        for (std::size_t k = 0; k < r.omega.size(); ++k) {
            const double w = r.omega[k] / kPi;
            if (w < lo_ - step || w > hi_ + step || (log_ && w < kLogFloor)) continue;
            tr_.x.push_back(to_axis(w));
            tr_.y.push_back(gd[k]);
            if (std::isfinite(gd[k]) && w >= lo_ && w <= hi_) lo = std::min(lo, gd[k]), hi = std::max(hi, gd[k]);
        }
        spec.traces.push_back(std::move(tr_));
    }
    if (!std::isfinite(hi)) hi = 1;
    if (hi - lo < 1) hi = lo + 1;
    spec.y = {lo, lo + (hi - lo) * 1.1 * delay_zoom_};
    auto readout = cursor_line(
        ts,
        [](const Trace& t, std::size_t k) {
            const auto& r = *t.response;
            return r.magnitude[k] < kMaskRelative * t.peak ? std::numeric_limits<double>::quiet_NaN() : r.group_delay[k];
        },
        "");
    return frame_panel(delay, vbox({plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | reflect(plot_boxes_[2]) | flex, readout}), {},
                       tr("tui.filters.delay"));
}

ftxui::Element FiltersScreen::impulse_panel(int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto* e = focused();
    if (!e || !e->analysis || e->analysis->h.coeffs.empty()) return frame_panel(impulse, text("—") | fg(th.dim, ui_), {}, tr("tui.filters.impulse"));
    const auto& h = e->analysis->h;
    PlotSpec spec;
    const double first = static_cast<double>(h.first()), last = static_cast<double>(h.last());
    spec.x = {first - 0.5, last + 0.5};
    double lo = 0, hi = 0;
    tui::Trace t{};
    t.stems = true;
    t.color = color_of(set_->focus());
    for (std::size_t k = 0; k < h.coeffs.size(); ++k) {
        t.x.push_back(first + static_cast<double>(k));
        t.y.push_back(h.coeffs[k]);
        lo = std::min(lo, h.coeffs[k]);
        hi = std::max(hi, h.coeffs[k]);
    }
    const double pad = std::max(1e-12, (hi - lo) * 0.1);
    spec.y = {lo - pad, hi + pad};
    spec.traces.push_back(std::move(t));
    std::string values;
    for (double c : h.coeffs) values += (values.empty() ? "" : " ") + std::string{c >= 0 ? "+" : ""} + format_fixed(c, 3);
    const auto color = color_of(set_->focus());
    Elements body{plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | flex};
    if (h.coeffs.size() <= 8 && display_width(values) <= width - 4) body.push_back(text(values) | fg(color, ui_));
    return frame_panel(impulse, vbox(std::move(body)), {}, tr("tui.filters.impulse") + " · " + e->ref.label());
}

ftxui::Element FiltersScreen::zeros_panel(int rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto* e = focused();
    if (!e || !e->analysis || !e->analysis->zp) return frame_panel(zeros, text("—") | fg(th.dim, ui_), {}, tr("tui.filters.zeros"));
    const auto& zp = *e->analysis->zp;
    const auto color = color_of(set_->focus());
    Elements list;
    const auto zero_roots = group_roots(zp.zeros);
    const auto pole_roots = group_roots(zp.poles);
    std::vector<std::pair<bool, Root>> roots;
    for (const auto& r : zero_roots) roots.emplace_back(false, r);
    for (const auto& r : pole_roots) roots.emplace_back(true, r);
    const int lines = std::max(1, rows - 2);
    const int room = std::max(1, lines - (zp.poles_at_origin > 0 ? 1 : 0));
    const bool clipped = static_cast<int>(roots.size()) > room;
    const auto shown = clipped ? static_cast<std::size_t>(room - 1) : roots.size();
    for (std::size_t i = 0; i < shown; ++i) {
        const auto mark = roots[i].first ? std::string{ui_.caps.glyphs == GlyphMode::ascii ? "x " : "× "} : std::string{ui_.caps.glyphs == GlyphMode::ascii ? "o " : "○ "};
        list.push_back(hbox({text(mark) | fg(color, ui_), text(root_text(roots[i].second)) | fg(th.fg, ui_)}));
    }
    if (clipped)
        list.push_back(text(fmt::format(fmt::runtime(tr("tui.filters.more_roots")), fmt::arg("count", roots.size() - shown))) | fg(th.dim, ui_));
    if (zp.poles_at_origin > 0)
        list.push_back(hbox({text("× ") | fg(th.fg, ui_), text("0" + (zp.poles_at_origin > 1 ? " ×" + std::to_string(zp.poles_at_origin) : std::string{})) |
                                                                   fg(th.fg, ui_)}));
    const int plane = layout_.maximized() ? std::clamp(2 * lines, 8, 2 * lines) : std::clamp(2 * lines, 8, 16);
    return frame_panel(zeros,
                       hbox({zplane(ZPlaneSpec{zp.zeros, zp.poles_at_origin, zp.poles}, th, ui_.caps.color, ui_.caps.glyphs) | size(WIDTH, EQUAL, plane),
                             text(" "), vbox(std::move(list))}),
                       {}, tr("tui.filters.zeros"));
}

ftxui::Element FiltersScreen::properties_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto* e = focused();
    if (!e || !e->analysis) return frame_panel(properties, text(e && e->error ? *e->error : "—") | fg(e && e->error ? th.err : th.dim, ui_), {},
                                               tr("tui.filters.properties"));
    const auto& a = *e->analysis;
    const bool spectral = a.ref.kind == FilterRef::Kind::scaling || a.ref.kind == FilterRef::Kind::wavelet_fn || a.ref.kind == FilterRef::Kind::cwt;
    if (spectral) {
        Elements rows;
        auto line = [&](const std::string& s) { rows.push_back(text(s) | fg(th.fg, ui_)); };
        auto format = [](const char* key, auto&&... args) { return fmt::format(fmt::runtime(tr(key)), std::forward<decltype(args)>(args)...); };
        if (a.ref.kind == FilterRef::Kind::cwt) {
            line(format("tui.filters.prop.cwt_centre", fmt::arg("centre", a.centre ? format_fixed(*a.centre, 3) : std::string{"—"}),
                        fmt::arg("scale", a.ref.level), fmt::arg("quality", a.quality ? format_fixed(*a.quality, 2) : std::string{"—"})));
            if (a.at_zero) line(format("tui.filters.prop.cwt_zero", fmt::arg("value", *a.at_zero < 1e-15 ? std::string{"0"} : format_number(*a.at_zero, 2))));
        } else {
            const bool phi = a.ref.kind == FilterRef::Kind::scaling;
            if (a.factors) line(format(a.ref.level ? "tui.filters.prop.factors" : "tui.filters.prop.factors_auto", fmt::arg("count", *a.factors)));
            if (a.zeros_at_minus_one) {
                const int n = *a.zeros_at_minus_one;
                line(phi ? format("tui.filters.prop.phi_zeros", fmt::arg("count", n)) : format("tui.filters.prop.psi_zeros", fmt::arg("count", n), fmt::arg("slope", 20 * n)));
            }
            if (a.partition_error)
                line(format(phi ? "tui.filters.prop.phi_partition" : "tui.filters.prop.psi_partition",
                            fmt::arg("value", *a.partition_error < kRoundOff ? std::string{"< 10⁻¹⁵"} : format_number(*a.partition_error, 2))));
        }
        return frame_panel(properties, vbox(std::move(rows)), {}, tr("tui.filters.properties") + " · " + e->ref.label());
    }
    const auto g = ui_.caps.glyphs;
    auto mark = [&](bool ok) { return text(" " + std::string{glyph(ok ? Glyph::check : Glyph::cross, g)}) | fg(ok ? th.ok : th.err, ui_); };
    Elements rows;
    if (a.zpk) {
        rows.push_back(hbox({text(fmt::format(fmt::runtime(tr("tui.filters.prop.iir")), fmt::arg("poles", a.zpk->p.size()), fmt::arg("sections", (a.zpk->p.size() + 1) / 2))) |
                                 fg(th.fg, ui_),
                             text(" · " + tr(a.stable.value_or(false) ? "tui.filters.stable" : "tui.filters.unstable")) | fg(th.fg, ui_), mark(a.stable.value_or(false))}));
    } else {
        rows.push_back(text(fmt::format(fmt::runtime(tr("tui.filters.prop.fir")), fmt::arg("taps", a.h.coeffs.size()))) | fg(th.fg, ui_));
    }
    const bool classical = a.ref.kind == FilterRef::Kind::classical;
    Elements orth;
    if (!classical && a.orth_error) {
        orth.push_back(text(tr("tui.filters.prop.orthogonal")) | fg(th.fg, ui_));
        orth.push_back(mark(*a.orth_error < kCheckTol));
    } else if (!classical) {
        orth.push_back(text(tr("tui.filters.prop.biorthogonal")) | fg(th.fg, ui_));
    }
    orth.push_back(text((classical ? "" : "   ") + tr("tui.filters.prop.linear_phase")) | fg(th.fg, ui_));
    orth.push_back(mark(a.linear_phase));
    rows.push_back(hbox(std::move(orth)));
    if (!classical) {
        rows.push_back(text(tr("tui.filters.prop.zeros") + " " + (a.zeros_at_minus_one ? std::to_string(*a.zeros_at_minus_one) : std::string{"—"})) |
                       fg(th.fg, ui_));
        rows.push_back(text(tr("tui.filters.prop.sobolev") + " " + (a.sobolev ? format_fixed(*a.sobolev, 3) : std::string{"—"})) | fg(th.fg, ui_));
    }
    if (a.pr_error)
        rows.push_back(hbox({text(tr("tui.filters.prop.pr") + " " + (*a.pr_error < kRoundOff ? std::string{"< 10⁻¹⁵"} : format_number(*a.pr_error, 2))) | fg(th.fg, ui_), mark(*a.pr_error < kCheckTol)}));
    const auto tiny = [](double v) { return std::abs(v) < 1e-12 ? std::string{"0"} : format_number(v, 4); };
    if (a.zpk) {
        const std::vector<double> ends{0.0, kPi};
        const auto gain = core::zpk_response(*a.zpk, ends);
        rows.push_back(text("|H(0)| = " + tiny(std::abs(gain[0])) + "   |H(π)| = " + tiny(std::abs(gain[1]))) | fg(th.fg, ui_));
    } else {
        double sum = 0, alternating = 0;
        for (std::size_t k = 0; k < a.h.coeffs.size(); ++k) {
            sum += a.h.coeffs[k];
            alternating += ((a.h.offset + static_cast<std::ptrdiff_t>(k)) % 2 == 0 ? 1.0 : -1.0) * a.h.coeffs[k];
        }
        rows.push_back(text("Σh = " + tiny(sum) + "   |H(π)| = " + tiny(std::abs(alternating))) | fg(th.fg, ui_));
    }
    return frame_panel(properties, vbox(std::move(rows)), {}, tr("tui.filters.properties") + " · " + e->ref.label());
}

ftxui::Element FiltersScreen::measures_panel(int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto g = ui_.caps.glyphs;
    const auto reference = set_->reference();
    const ComparisonSet::Entry* ref = reference && *reference < entries_.size() && entries_[*reference].analysis ? &entries_[*reference] : nullptr;
    auto flag = [&](std::optional<bool> ok, int w) {
        if (!ok) return text(pad_left("—", w)) | fg(th.dim, ui_);
        return text(pad_left(std::string{glyph(*ok ? Glyph::check : Glyph::cross, g)}, w)) | fg(*ok ? th.ok : th.err, ui_);
    };
    auto cell = [&](const std::string& s, int w) { return text(pad_left(s, w)) | fg(th.fg, ui_); };
    struct Column {
        std::string header;
        int width;
        int priority;
        std::function<Element(const ComparisonSet::Entry&)> draw;
    };
    std::vector<Column> columns{
        {tr("tui.filters.col.cutoff"), 9, 1, [&](const auto& e) { return cell(pi_text(e.analysis->metrics.cutoff_3db), 9); }},
        {tr("tui.filters.col.ripple"), 11, 9,
         [&](const auto& e) {
             const double v = e.analysis->metrics.passband_ripple_db;
             return cell(std::isfinite(v) ? format_fixed(v, 2) + " " + tr("tui.filters.db") : "—", 11);
         }},
        {tr("tui.filters.col.stop"), 12, 2,
         [&](const auto& e) {
             const double v = e.analysis->metrics.stopband_attenuation_db;
             return cell(std::isfinite(v) ? format_fixed(v, 1) + " " + tr("tui.filters.db") : "—", 12);
         }},
        {tr("tui.filters.col.transition"), 10, 8, [&](const auto& e) { return cell(pi_text(e.analysis->metrics.transition_width), 10); }},
        {tr("tui.filters.col.vm"), 4, 5,
         [&](const auto& e) { return cell(e.analysis->zeros_at_minus_one ? std::to_string(*e.analysis->zeros_at_minus_one) : "—", 4); }},
        {"PR", 4, 4, [&](const auto& e) { return flag(e.analysis->pr_error ? std::optional<bool>{*e.analysis->pr_error < kCheckTol} : std::nullopt, 4); }},
        {tr("tui.filters.col.orth"), 6, 6,
         [&](const auto& e) { return flag(e.analysis->orth_error ? std::optional<bool>{*e.analysis->orth_error < kCheckTol} : std::nullopt, 6); }},
        {tr("tui.filters.col.linear"), 6, 7, [&](const auto& e) { return flag(e.analysis->linear_phase, 6); }},
    };
    if (ref) {
        columns.push_back({"Δ " + tr("tui.filters.col.cutoff"), 11, 3, [&](const auto& e) {
                               if (&e == ref) return text(pad_left(tr("tui.filters.reference_mark"), 11)) | fg(th.accent, ui_);
                               const double d = e.analysis->metrics.cutoff_3db - ref->analysis->metrics.cutoff_3db;
                               return cell(std::isfinite(d) ? signed_text(d, 3) + "π" : "—", 11);
                           }});
        columns.push_back({"Δ " + tr("tui.filters.col.stop_short"), 11, 3, [&](const auto& e) {
                               if (&e == ref) return text(pad_left("", 11));
                               return cell(signed_text(e.analysis->metrics.stopband_attenuation_db - ref->analysis->metrics.stopband_attenuation_db, 1), 11);
                           }});
    }
    std::vector<std::size_t> order(columns.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return columns[a].priority < columns[b].priority; });
    std::vector<bool> keep(columns.size(), false);
    int room = std::max(0, width - 4 - 16);
    std::size_t dropped = 0;
    for (auto i : order) {
        if (columns[i].width <= room) {
            keep[i] = true;
            room -= columns[i].width;
        } else {
            ++dropped;
        }
    }
    Elements header{text(pad_right(tr("tui.filters.col.filter"), 16)) | fg(th.dim, ui_)};
    for (std::size_t i = 0; i < columns.size(); ++i)
        if (keep[i]) header.push_back(text(pad_left(columns[i].header, columns[i].width)) | fg(th.dim, ui_));
    Elements rows{hbox(std::move(header))};
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!e.visible || !e.analysis) continue;
        Elements row{text(pad_right(e.ref.label(), 16)) | fg(color_of(i), ui_)};
        for (std::size_t c = 0; c < columns.size(); ++c)
            if (keep[c]) row.push_back(columns[c].draw(e));
        rows.push_back(hbox(std::move(row)));
    }
    if (rows.size() == 1) rows.push_back(text("—") | fg(th.dim, ui_));
    const auto title = tr("tui.filters.measures") + (dropped ? fmt::format(fmt::runtime(tr("tui.filters.more_columns")), fmt::arg("count", dropped)) : "");
    return frame_panel(measures, vbox(std::move(rows)), {{tr("tui.filters.chip.bands"), false}}, title);
}

ftxui::Element FiltersScreen::build(Panel p, int rows, int width) {
    switch (p) {
    case magnitude: return magnitude_panel(width);
    case phase: return phase_panel();
    case delay: return delay_panel();
    case impulse: return impulse_panel(width);
    case zeros: return zeros_panel(rows);
    case properties: return properties_panel();
    case measures: return measures_panel(width);
    case library: return library_panel(width);
    case comparison: return comparison_panel(width);
    case panel_count: break;
    }
    return ftxui::text("");
}

ftxui::Element FiltersScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    title_boxes_.fill(kNoBox);
    plot_boxes_.fill(kNoBox);
    clickables_.clear();
    if (ui_.jobs) {
        set_->request(*ui_.jobs, kPoints);
        if (lo_ > 0 || hi_ < span_) set_->request_view(*ui_.jobs, lo_, hi_, kPoints);
    }
    entries_ = set_->entries();
    if (const auto w = focus_wavelet(); w != library_wavelet_) {
        library_wavelet_ = w;
        library_ = build_library(w);
    }
    double span = 1;
    for (const auto& e : entries_)
        if (e.visible) span = std::max(span, span_of(e.ref));
    if (span != span_) {
        span_ = span;
        lo_ = 0;
        hi_ = span_;
        cursor_a_.reset();
        cursor_b_.reset();
    }
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const int height = std::max(3, rows - 3);
    if (breakpoint_ <= Breakpoint::small || layout_.maximized()) {
        const Panel p = layout_.maximized() ? panel_of(*layout_.maximized()) : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        return build(p, height, cols);
    }
    int& left = layout_.size("left");
    left = std::clamp(left, kMinLeft, std::max(kMinLeft, cols / 3));
    const int right = cols - left - 1;
    std::size_t shown = 0;
    for (const auto& e : entries_) shown += e.visible && e.analysis ? 1 : 0;
    const int measures_rows = 3 + static_cast<int>(std::clamp<std::size_t>(shown, 1, ComparisonSet::kCapacity));
    int gaps = 3;
    int magnitude_rows = height - 2 * kRowPanels - measures_rows - gaps;
    if (magnitude_rows < 12) {
        gaps = 0;
        magnitude_rows = height - 2 * kRowPanels - measures_rows;
    }
    magnitude_rows = std::max(8, magnitude_rows);
    const int half = (right - 1) / 2;
    const int impulse_width = std::clamp(right * 2 / 7, 24, 40);
    const int zeros_width = std::clamp(right / 4, 24, 34);
    auto gap = [&](Elements& v) {
        if (gaps) v.push_back(text(""));
    };
    Elements right_column{build(magnitude, magnitude_rows, right) | size(HEIGHT, EQUAL, magnitude_rows)};
    gap(right_column);
    right_column.push_back(hbox({build(phase, kRowPanels, half) | size(WIDTH, EQUAL, half), text(" "), build(delay, kRowPanels, right - half - 1) | flex}) |
                           size(HEIGHT, EQUAL, kRowPanels));
    gap(right_column);
    right_column.push_back(hbox({build(impulse, kRowPanels, impulse_width) | size(WIDTH, EQUAL, impulse_width), text(" "),
                                 build(zeros, kRowPanels, zeros_width) | size(WIDTH, EQUAL, zeros_width), text(" "),
                                 build(properties, kRowPanels, right - impulse_width - zeros_width - 2) | flex}) |
                           size(HEIGHT, EQUAL, kRowPanels));
    gap(right_column);
    right_column.push_back(build(measures, measures_rows, right) | flex);
    auto left_column = vbox({build(library, height - kComparisonRows - 1, left) | flex, text(""),
                             build(comparison, kComparisonRows, left) | size(HEIGHT, EQUAL, kComparisonRows)});
    return hbox({left_column | size(WIDTH, EQUAL, left), text(" "), vbox(std::move(right_column)) | flex});
}

void FiltersScreen::zoom(double factor, double anchor) {
    if (!(factor > 0)) return;
    if (log_) {
        const double l = std::log(std::max(lo_, kLogFloor)), h = std::log(hi_), a = std::log(std::clamp(anchor, kLogFloor, span_));
        const double nl = a - (a - l) / factor, nh = a + (h - a) / factor;
        double lo = std::exp(nl), hi = std::min(span_, std::exp(nh));
        if (hi - lo < kMinSpan) return;
        lo_ = lo <= kLogFloor * 1.0001 ? 0.0 : lo;
        hi_ = hi;
        return;
    }
    const double span = hi_ - lo_;
    const double s = std::clamp(span / factor, kMinSpan, span_);
    const double a = std::clamp(anchor, lo_, hi_);
    const double lo = std::clamp(a - (a - lo_) * s / span, 0.0, span_ - s);
    lo_ = lo;
    hi_ = lo + s;
}

void FiltersScreen::pan(double delta) {
    const double span = hi_ - lo_;
    lo_ = std::clamp(lo_ + delta, 0.0, span_ - span);
    hi_ = lo_ + span;
}

bool FiltersScreen::on_mouse(const ftxui::Mouse& m) {
    using ftxui::Mouse;
    if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
        const int p = plot_under(m.x, m.y);
        if (p < 0) return false;
        const double f = m.button == Mouse::WheelUp ? kWheelStep : 1 / kWheelStep;
        if (m.shift) {
            if (p == 0 && decibels_) db_floor_ = std::clamp(db_floor_ / f, -300.0, -6.0);
            else if (p == 0) linear_zoom_ = std::clamp(linear_zoom_ / f, 0.01, 1.0);
            else if (p == 1) phase_zoom_ = std::clamp(phase_zoom_ / f, 1e-3, 1.0);
            else delay_zoom_ = std::clamp(delay_zoom_ / f, 1e-3, 1.0);
        } else {
            zoom(f, omega_at(p == 1 ? "phase" : p == 2 ? "delay" : "magnitude", m.x));
        }
        return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        for (auto& c : clickables_)
            if (c.box.Contain(m.x, m.y)) {
                auto action = c.action;
                action();
                return true;
            }
        bool hit = false;
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) focus_ = static_cast<Panel>(p), hit = true;
        const int p = plot_under(m.x, m.y);
        if (p >= 0) {
            drag_ = {true, false, m.shift, p, m.x};
            return true;
        }
        return hit;
    }
    if (!drag_.active) return false;
    const char* name = drag_.plot == 1 ? "phase" : drag_.plot == 2 ? "delay" : "magnitude";
    if (m.motion == Mouse::Released) {
        if (!drag_.moved) {
            const double w = omega_at(name, m.x);
            if (drag_.shift) cursor_b_ = w;
            else cursor_a_ = w;
        }
        drag_.active = false;
        return true;
    }
    if (m.x != drag_.last_x) {
        drag_.moved = true;
        const auto a = plot_box(drag_.plot);
        const int columns = std::max(1, a.x_max - a.x_min + 1);
        pan((drag_.last_x - m.x) * (hi_ - lo_) / columns);
        drag_.last_x = m.x;
    }
    return true;
}

std::vector<MenuItem> FiltersScreen::context_menu(int x, int y) {
    if (const int p = plot_under(x, y); p >= 0) {
        const double w = omega_at(p == 1 ? "phase" : p == 2 ? "delay" : "magnitude", x);
        return {{tr("tui.menu.cursor_a"), tr("tui.signal.key.click"), [this, w] { cursor_a_ = w; }},
                {tr("tui.menu.cursor_b"), "Shift+" + tr("tui.signal.key.click"), [this, w] { cursor_b_ = w; }},
                {tr("tui.menu.reset_zoom"), "", [this] {
                     lo_ = 0;
                     hi_ = span_;
                     phase_zoom_ = delay_zoom_ = linear_zoom_ = 1;
                     db_floor_ = -80;
                 }}};
    }
    for (std::size_t i = 0; i < row_boxes_.size() && i < entries_.size(); ++i) {
        if (!row_boxes_[i].Contain(x, y)) continue;
        const bool is_reference = set_->reference() == i;
        return {{tr(is_reference ? "tui.menu.clear_reference" : "tui.menu.make_reference"), "r",
                 [this, i, is_reference] { set_->set_reference(is_reference ? std::nullopt : std::optional<std::size_t>{i}); }},
                {tr(entries_[i].visible ? "tui.menu.hide" : "tui.menu.show"), "Space", [this, i] { set_->toggle(i); }},
                {tr("tui.menu.remove"), "Del", [this, i] { set_->remove(i); }}};
    }
    return {};
}

std::vector<JumpTarget> FiltersScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, ""});
    for (int p = 0; p < panel_count; ++p) out.push_back({title_boxes_[static_cast<std::size_t>(p)], key_of(static_cast<Panel>(p))});
    return out;
}

bool FiltersScreen::on_search(ftxui::Event e) {
    using ftxui::Event;
    if (e.is_mouse()) return on_mouse(e.mouse());
    if (e == Event::Escape) {
        searching_ = false;
        query_.clear();
        return true;
    }
    if (e == Event::Return) {
        const auto m = matches();
        if (!m.empty()) add(*m[std::min(match_index_, m.size() - 1)]->ref);
        searching_ = false;
        query_.clear();
        return true;
    }
    if (e == Event::Backspace) {
        pop_codepoint(query_);
        match_index_ = 0;
        return true;
    }
    if (e == Event::ArrowRight || e == Event::ArrowDown) {
        const auto n = matches().size();
        if (n) match_index_ = std::min(match_index_ + 1, n - 1);
        return true;
    }
    if (e == Event::ArrowLeft || e == Event::ArrowUp) {
        if (match_index_ > 0) --match_index_;
        return true;
    }
    if (e == Event::PageDown || e == Event::End) {
        const auto n = matches().size();
        if (n) match_index_ = e == Event::End ? n - 1 : std::min(match_index_ + 10, n - 1);
        return true;
    }
    if (e == Event::PageUp || e == Event::Home) {
        match_index_ = e == Event::Home ? 0 : match_index_ - std::min<std::size_t>(match_index_, 10);
        return true;
    }
    if (e.is_character()) {
        query_ += e.character();
        match_index_ = 0;
    }
    return true;
}

bool FiltersScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (searching_) return on_search(e);
    if (e.is_mouse()) return on_mouse(e.mouse());
    if (e == Event::Character('/')) {
        searching_ = true;
        query_.clear();
        match_index_ = 0;
        if (breakpoint_ <= Breakpoint::small && !layout_.maximized())
            for (std::size_t k = 0; k < panel_count && layout_.small_index() % panel_count != library; ++k) layout_.next_panel(panel_count);
        return true;
    }
    if (e == Event::Character('t')) {
        add_role_ = static_cast<FilterRole>((static_cast<int>(add_role_) + 1) % 4);
        return true;
    }
    if (e == Event::Character(' ')) {
        set_->toggle(set_->focus());
        return true;
    }
    if (e == Event::Character('r')) {
        const auto f = set_->focus();
        set_->set_reference(set_->reference() == f ? std::nullopt : std::optional<std::size_t>{f});
        return true;
    }
    if (e == Event::Delete) {
        set_->remove(set_->focus());
        return true;
    }
    if (e == Event::ArrowUp) {
        if (set_->focus() > 0) set_->set_focus(set_->focus() - 1);
        return true;
    }
    if (e == Event::ArrowDown) {
        set_->set_focus(set_->focus() + 1);
        return true;
    }
    if (e == Event::ArrowLeft || e == Event::ArrowRight) {
        const auto a = plot_box(0);
        const int columns = valid(a) ? a.x_max - a.x_min + 1 : 100;
        const double step = (hi_ - lo_) / (2.0 * columns);
        auto& cursor = arrows_move_b_ ? cursor_b_ : cursor_a_;
        const double current = cursor.value_or((lo_ + hi_) / 2);
        cursor = std::clamp(current + (e == Event::ArrowLeft ? -step : step), 0.0, span_);
        if (*cursor < lo_) pan(*cursor - lo_);
        if (*cursor > hi_) pan(*cursor - hi_);
        return true;
    }
    if (e == Event::Character('b')) {
        arrows_move_b_ = !arrows_move_b_;
        if (arrows_move_b_ && !cursor_b_) cursor_b_ = cursor_a_.value_or((lo_ + hi_) / 2);
        return true;
    }
    if (e == Event::Character('+') || e == Event::Character('=') || e == Event::Character('-')) {
        const double anchor = cursor_a_ && *cursor_a_ >= lo_ && *cursor_a_ <= hi_ ? *cursor_a_ : (lo_ + hi_) / 2;
        zoom(e == Event::Character('-') ? 1 / kWheelStep : kWheelStep, anchor);
        return true;
    }
    if (e == Event::Character('>') || e == Event::Character('.')) return pan(0.2 * (hi_ - lo_)), true;
    if (e == Event::Character('<') || e == Event::Character(',')) return pan(-0.2 * (hi_ - lo_)), true;
    if (e == Event::Character('0')) {
        lo_ = 0;
        hi_ = span_;
        phase_zoom_ = delay_zoom_ = linear_zoom_ = 1;
        db_floor_ = -80;
        return true;
    }
    if (e == Event::Character(']') || e == Event::Character('[')) {
        const double f = e == Event::Character(']') ? kWheelStep : 1 / kWheelStep;
        if (focus_ == phase) phase_zoom_ = std::clamp(phase_zoom_ / f, 1e-3, 1.0);
        else if (focus_ == delay) delay_zoom_ = std::clamp(delay_zoom_ / f, 1e-3, 1.0);
        else if (decibels_) db_floor_ = std::clamp(db_floor_ / f, -300.0, -6.0);
        else linear_zoom_ = std::clamp(linear_zoom_ / f, 0.01, 1.0);
        return true;
    }
    if (e == Event::Return && focus_ == library) {
        searching_ = true;
        query_.clear();
        match_index_ = 0;
        return true;
    }
    if (e == Event::Character('l')) {
        log_ = !log_;
        return true;
    }
    if (e == Event::Character('d')) {
        decibels_ = !decibels_;
        return true;
    }
    if (e == Event::Character('o') || e == Event::Character('O') || e == Event::Character('f') || e == Event::Character('F')) {
        const auto entries = set_->entries();
        const auto i = set_->focus();
        if (i >= entries.size()) return false;
        const int step = e == Event::Character('o') ? 1 : e == Event::Character('O') ? -1 : 0;
        const double factor = e == Event::Character('f') ? 1.1 : e == Event::Character('F') ? 1 / 1.1 : 1.0;
        auto next = classical_step(entries[i].ref, step, factor);
        if (!next && step != 0) next = spectrum_step(entries[i].ref, step);
        if (!next) return false;
        (void)set_->replace(i, *next);
        return true;
    }
    if (e == Event::Character('n')) {
        normalise_ = !normalise_;
        return true;
    }
    if (e == Event::Character('z')) {
        layout_.toggle_maximize(key_of(focus_));
        return true;
    }
    if (e == Event::Escape && layout_.maximized()) {
        layout_.toggle_maximize(*layout_.maximized());
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse) {
        const bool back = e == Event::TabReverse;
        const std::size_t step = back ? panel_count - 1 : 1;
        if (layout_.maximized()) {
            focus_ = static_cast<Panel>((panel_of(*layout_.maximized()) + step) % panel_count);
            layout_.toggle_maximize(key_of(focus_));
        } else if (breakpoint_ <= Breakpoint::small) {
            layout_.next_panel(panel_count, back);
        } else {
            focus_ = static_cast<Panel>((focus_ + step) % panel_count);
        }
        return true;
    }
    return false;
}

std::optional<std::pair<int, int>> FiltersScreen::menu_anchor() const {
    const auto& b = panel_boxes_[static_cast<std::size_t>(focus_)];
    if (b.x_max < b.x_min) return std::nullopt;
    return std::pair{(b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2};
}

std::shared_ptr<TabScreen> make_filters_screen(UiContext& ui, std::shared_ptr<ComparisonSet> set) {
    return std::make_shared<FiltersScreen>(ui, std::move(set));
}

}  // namespace signals::tui
