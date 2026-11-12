#include "tui/screens/explain_screen.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <fmt/args.h>
#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/freq.hpp"
#include "core/laurent.hpp"
#include "core/wavelet/regularity.hpp"
#include "i18n/i18n.hpp"
#include "tui/widgets/bank_diagram.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

const std::vector<double> kX{4, 6, 10, 12, 8, 6, 5, 5};
constexpr auto kAutoStep = std::chrono::milliseconds{2500};
constexpr int kStepsWidth = 31;
constexpr int kNameWidth = 6;

std::string vulgar(double c) {
    static const std::pair<double, const char*> table[]{{0.5, "½"},   {0.25, "¼"}, {0.75, "¾"},  {0.125, "⅛"}, {0.375, "⅜"},
                                                        {0.625, "⅝"}, {0.875, "⅞"}, {1.0 / 3, "⅓"}, {2.0 / 3, "⅔"}};
    if (std::abs(c - 1) < 1e-12) return {};
    for (const auto& [v, s] : table)
        if (std::abs(c - v) < 1e-12) return s;
    return format_number(c, 4);
}

std::string power_text(long n) { return n < 0 ? "⁺" + superscript_count(static_cast<int>(-n)) : "⁻" + superscript_count(static_cast<int>(n)); }

std::string p0_text(const core::FilterBank& b) {
    const auto p = core::trim(core::multiply(b.f0, b.h0), 1e-12);
    if (p.coeffs.empty()) return "0";
    const int k = core::zeros_at_minus_one(p, 1e-6);
    const auto binom = core::power(core::Laurent{{1.0, 1.0}, 0}, k);
    const std::string factor = "(1 + z⁻¹)" + (k > 1 ? superscript_count(k) : std::string{});
    if (p.coeffs.size() == binom.coeffs.size()) {
        const double c = p.coeffs[0] / binom.coeffs[0];
        bool exact = true;
        for (std::size_t i = 0; i < p.coeffs.size(); ++i) exact = exact && std::abs(p.coeffs[i] - c * binom.coeffs[i]) < 1e-9;
        if (exact) return vulgar(c) + factor;
    }
    return factor + "·Q(z), deg Q = " + std::to_string(static_cast<long>(p.coeffs.size()) - 1 - k);
}

std::size_t nearest(const std::vector<double>& omega, double w) {
    std::size_t best = 0;
    for (std::size_t k = 1; k < omega.size(); ++k)
        if (std::abs(omega[k] - w) < std::abs(omega[best] - w)) best = k;
    return best;
}

std::string db_text(double db) { return std::isinf(db) ? "−∞" : format_fixed(db, 1); }

}  // namespace

ExplainScreen::ExplainScreen(UiContext& ui, std::function<std::optional<core::FilterBank>()> user_bank)
    : ui_{ui}, user_bank_{std::move(user_bank)} {
    panel_boxes_.fill(kNoBox);
    step_boxes_.fill(kNoBox);
    bank_boxes_.fill(kNoBox);
    prev_box_ = next_box_ = auto_box_ = kNoBox;
}

std::string ExplainScreen::tab_label() const { return tr("tui.sub.explain"); }

std::vector<Hint> ExplainScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},    {"z", tr("tui.signal.hint.zoom")},       {"←→ ↑↓", tr("tui.explain.hint.step")},
            {"PgUp/PgDn", tr("tui.explain.hint.three_steps")}, {"Home/End", tr("tui.hint.ends")},  {"+ − 0 < >", tr("tui.hint.plot_zoom")},
            {"Space", tr("tui.explain.hint.auto")}, {"w", tr("tui.explain.hint.bank")},      {"p", tr("tui.explain.hint.phase")},
            {"e", tr("tui.explain.hint.perturb")},  {"f", tr("tui.explain.hint.fractions")}, {"Enter", tr("tui.explain.hint.open")},
            {"F1", tr("tui.explain.hint.f1")},      {tr("tui.signal.key.wheel"), tr("tui.zoom.hint")}};
}

std::string ExplainScreen::context() const { return tr("tui.explain.context"); }

std::chrono::steady_clock::time_point ExplainScreen::now() const { return ui_.now ? ui_.now() : std::chrono::steady_clock::now(); }

const Walkthrough* ExplainScreen::current() {
    if (dirty_) {
        if (options_.bank == ExplainBank::user) {
            options_.user = user_bank_ ? user_bank_() : std::nullopt;
            if (!options_.user) options_.bank = ExplainBank::haar;
        }
        auto w = walkthrough(kX, options_);
        if (w) {
            walkthrough_ = std::move(*w);
            error_.reset();
        } else {
            walkthrough_.reset();
            error_ = w.error().message;
        }
        dirty_ = false;
    }
    return walkthrough_ ? &*walkthrough_ : nullptr;
}

void ExplainScreen::set_step(std::size_t step) {
    step_ = std::clamp<std::size_t>(step, 1, 12);
    const bool zero = step_ == 12;
    if (options_.zero_detail != zero) {
        options_.zero_detail = zero;
        dirty_ = true;
    }
}

void ExplainScreen::set_bank(ExplainBank bank) {
    if (bank == ExplainBank::user && !(user_bank_ && user_bank_())) return;
    options_.bank = bank;
    dirty_ = true;
}

void ExplainScreen::cycle_bank() {
    switch (options_.bank) {
    case ExplainBank::haar: set_bank(ExplainBank::db2); break;
    case ExplainBank::db2: set_bank(ExplainBank::legall53); break;
    case ExplainBank::legall53:
        if (user_bank_ && user_bank_()) set_bank(ExplainBank::user);
        else set_bank(ExplainBank::haar);
        break;
    case ExplainBank::user: set_bank(ExplainBank::haar); break;
    }
}

std::string ExplainScreen::wavelet_name() const {
    switch (options_.bank) {
    case ExplainBank::haar: return "haar";
    case ExplainBank::db2: return "db2";
    case ExplainBank::legall53: return "bior2.2";
    case ExplainBank::user: return {};
    }
    return {};
}

void ExplainScreen::open_with(const NavArgs& args) {
    if (args.step) set_step(*args.step);
    if (args.wavelet == "haar") set_bank(ExplainBank::haar);
    else if (args.wavelet == "db2") set_bank(ExplainBank::db2);
    else if (args.wavelet == "bior2.2") set_bank(ExplainBank::legall53);
}

ftxui::Element ExplainScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    static constexpr const char* kNames[]{"scheme", "steps", "story", "matrix", "freq", "p0"};
    const std::string name = kNames[static_cast<std::size_t>(p)];
    for (auto& c : chips)
        if (!c.box) c.box = notes_.box("legend." + name);
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1),
                                     fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_, notes_.box("panel." + name)) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element ExplainScreen::rows_table(const Walkthrough& w, int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& step = haar_script()[step_ - 1];
    const int cw = breakpoint_ <= Breakpoint::small ? 7 : 8;
    struct Row {
        std::string name;
        std::vector<double> values;
        Rgb color;
        bool faint = false;
        bool keep = false;
        std::string note;
    };
    auto slice = [&](const std::vector<double>& v) {
        std::vector<double> out;
        for (std::size_t n = 0; n < w.x.size(); ++n) out.push_back(w.window + n < v.size() ? v[w.window + n] : 0.0);
        return out;
    };
    auto note = [&](const char* key, std::initializer_list<std::pair<const char*, std::string>> args) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for (const auto& [k, v] : args) store.push_back(fmt::arg(k, v));
        return fmt::vformat(tr(key), store);
    };
    auto filled = [&](std::string_view s) {
        auto r = fill(s, w);
        return r ? *r : std::string{s};
    };
    const bool faint_u = step_ >= 8;
    std::vector<Row> rows;
    for (const auto& id : step.rows) {
        if (id == "x") rows.push_back({"x", w.x, th.fg, false, false, note("tui.explain.note.energy", {{"name", "x"}, {"value", filled("{{x.energy}}")}})});
        else if (id == "y0")
            rows.push_back({"y₀", w.y0, th.trace[0], false, step.mark_kept,
                            step.mark_kept ? note("tui.explain.note.kept", {{"parity", filled("{{parity}}")}}) : note("tui.explain.note.conv", {{"filter", "h₀"}})});
        else if (id == "y1")
            rows.push_back({"y₁", w.y1, th.trace[1], false, step.mark_kept,
                            step.mark_kept ? note("tui.explain.note.indices", {{"kept", filled("{{kept}}")}}) : note("tui.explain.note.conv", {{"filter", "h₁"}})});
        else if (id == "a") rows.push_back({"a", w.a, th.trace[0], false, false, note("tui.explain.note.energy", {{"name", "a"}, {"value", filled("{{a.energy}}")}})});
        else if (id == "d") {
            if (options_.zero_detail) rows.push_back({"d", std::vector<double>(w.d.size(), 0.0), th.trace[1], false, false, tr("tui.explain.note.zeroed")});
            else rows.push_back({"d", w.d, th.trace[1], false, false, note("tui.explain.note.energy", {{"name", "d"}, {"value", filled("{{d.energy}}")}})});
        } else if (id == "u0") rows.push_back({"u₀", w.u0, th.trace[0], faint_u, false, {}});
        else if (id == "u1") rows.push_back({"u₁", w.u1, th.trace[1], faint_u, false, {}});
        else if (id == "w0") rows.push_back({"w₀", slice(w.w0), th.trace[0], false, false, {}});
        else if (id == "w1") rows.push_back({"w₁", slice(w.w1), th.trace[1], false, false, {}});
        else if (id == "xh") {
            if (options_.zero_detail) {
                rows.push_back({"x̂", slice(w.w0), th.accent, false, false, tr("tui.explain.note.zero_detail")});
            } else {
                const auto inv = check_invariant("reconstruction", w);
                rows.push_back({"x̂", slice(w.xhat), th.accent, false, false,
                                note("tui.explain.note.error", {{"error", filled("{{error}}")}}) + " " +
                                    std::string{glyph(inv.passed ? Glyph::check : Glyph::cross, ui_.caps.glyphs)}});
            }
        } else if (id == "tree") {
            const auto levels = w.tree.size() - (w.tree.empty() ? 0 : 1);
            for (std::size_t k = 0; k < w.tree.size(); ++k)
                rows.push_back({band_name(k, levels, ui_.caps.glyphs), w.tree[k], k == 0 ? th.trace[0] : th.trace[1], false, false, {}});
        }
    }
    std::size_t cells = 0;
    for (const auto& r : rows) cells = std::max(cells, r.values.size());
    const auto fit = static_cast<std::size_t>(std::max(2, (width - kNameWidth - 2) / cw));
    const bool clipped = cells > fit;
    const std::size_t shown = clipped ? fit - 1 : cells;
    Elements out;
    {
        std::string idx = pad_right(tr("tui.explain.index"), kNameWidth);
        for (std::size_t i = 0; i < shown; ++i) idx += pad_left(std::to_string(i), cw);
        if (clipped) idx += pad_left("…", cw);
        out.push_back(text(idx) | fg(th.dim, ui_));
    }
    for (const auto& r : rows) {
        Elements parts{text(pad_right(r.name, kNameWidth)) | bold | fg(r.color, ui_)};
        for (std::size_t i = 0; i < shown && i < r.values.size(); ++i) {
            const bool kept = r.keep && std::find(w.kept.begin(), w.kept.end(), i) != w.kept.end();
            auto cell = text(pad_left(format_cell(r.values[i], options_.summing), cw));
            if (kept) cell = cell | fg(th.bg, ui_) | bg(th.accent, ui_);
            else cell = cell | fg(r.faint ? th.dim : th.fg, ui_);
            parts.push_back(cell);
        }
        int used = kNameWidth + static_cast<int>(std::min(shown, r.values.size())) * cw;
        if (clipped && r.values.size() > shown) {
            parts.push_back(text(pad_left("…", cw)) | fg(th.dim, ui_));
            used += cw;
        }
        const int room = width - used - 3;
        if (!r.note.empty() && room >= 4) {
            parts.push_back(text("   " + ellipsize(r.note, room, ui_.caps.glyphs == GlyphMode::ascii)) | fg(th.dim, ui_));
        }
        out.push_back(hbox(std::move(parts)));
    }
    return vbox(std::move(out));
}

ftxui::Element ExplainScreen::steps_body() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& script = haar_script();
    Elements out{text(tr("tui.explain.steps")) | fg(th.dim, ui_)};
    for (std::size_t i = 0; i < script.size() && i < step_boxes_.size(); ++i) {
        auto label = text(pad_left(std::to_string(i + 1), 2) + "  " + script[i].title);
        if (i + 1 == step_) label = label | bold | fg(th.bg, ui_) | bg(th.accent, ui_);
        else label = label | fg(i + 1 < step_ ? th.fg : th.dim, ui_);
        out.push_back(label | reflect(step_boxes_[i]));
    }
    out.push_back(text(""));
    out.push_back(hbox({text(tr("tui.explain.prev")) | fg(th.fg, ui_) | reflect(prev_box_), text("  "),
                        text(tr("tui.explain.next")) | fg(th.fg, ui_) | reflect(next_box_), text("  "),
                        text(tr(auto_ ? "tui.explain.auto_on" : "tui.explain.auto")) | fg(auto_ ? th.accent : th.fg, ui_) | reflect(auto_box_)}));
    return vbox(std::move(out));
}

ftxui::Element ExplainScreen::scheme_panel(const Walkthrough& w, int width, bool with_steps) {
    using namespace ftxui;
    const auto& step = haar_script()[step_ - 1];
    DiagramSpec spec;
    spec.highlight.insert(step.highlight.begin(), step.highlight.end());
    const int left_width = std::max(20, width - 4 - (with_steps ? kStepsWidth + 2 : 0));
    auto left = vbox({bank_diagram(spec, ui_), text(""), rows_table(w, left_width)});
    Element body = left;
    if (with_steps)
        body = hbox({left | flex, text(" "), separator() | fg(ui_.theme.dim, ui_), text(" "), steps_body() | size(WIDTH, EQUAL, kStepsWidth)});
    const std::string bank = options_.bank == ExplainBank::haar ? tr("tui.explain.bank.haar") : w.bank_name;
    const auto title = fmt::format(fmt::runtime(tr("tui.explain.title")), fmt::arg("bank", bank), fmt::arg("step", step_),
                                   fmt::arg("title", step.title));
    std::vector<Chip> chips;
    const std::pair<ExplainBank, std::string> banks[]{{ExplainBank::haar, tr("tui.explain.chip.haar")},
                                                      {ExplainBank::db2, "db2"},
                                                      {ExplainBank::legall53, "5/3"},
                                                      {ExplainBank::user, tr("tui.explain.chip.user")}};
    for (std::size_t i = 0; i < 4; ++i) {
        if (banks[i].first == ExplainBank::user && !(user_bank_ && user_bank_())) continue;
        chips.push_back({banks[i].second, options_.bank == banks[i].first, &bank_boxes_[i]});
    }
    chips.push_back({tr("tui.explain.chip.change"), false});
    return frame_panel(scheme, body, std::move(chips), title);
}

ftxui::Element ExplainScreen::text_panel(const Walkthrough& w) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& step = haar_script()[step_ - 1];
    const bool canvas = options_.bank == ExplainBank::haar && !options_.summing && !step.text_haar.empty();
    const auto& lines = canvas ? step.text_haar : step.text;
    const auto prefix = tr("tui.explain.invariant_prefix");
    Elements out;
    std::string para;
    const auto flush = [&] {
        if (!para.empty()) out.push_back(paragraph(para) | fg(th.fg, ui_));
        para.clear();
    };
    for (const auto& l : lines) {
        auto f = fill(l, w);
        std::string s = f ? *f : l;
        if (s.starts_with(prefix) && !step.invariant.empty()) {
            flush();
            const auto inv = check_invariant(step.invariant, w);
            if (!inv.checked) {
                out.push_back(paragraph(s) | fg(th.fg, ui_));
                continue;
            }
            s += inv.passed ? " " + std::string{glyph(Glyph::check, ui_.caps.glyphs)}
                            : " " + std::string{glyph(Glyph::cross, ui_.caps.glyphs)} + (std::isnan(inv.value) ? std::string{} : " " + format_number(inv.value, 2));
            out.push_back(paragraph(s) | fg(inv.passed ? th.ok : th.err, ui_));
        } else if (s.empty()) {
            flush();
            out.push_back(text(""));
        } else {
            para += (para.empty() ? "" : " ") + s;
        }
    }
    flush();
    auto body = vbox(std::move(out)) | focusPositionRelative(0.f, story_scroll_) | yframe;
    return frame_panel(story, body, {{tr("tui.explain.chip.invariant"), false}}, tr("tui.explain.text"));
}

ftxui::Element ExplainScreen::matrix_panel(const Walkthrough& w) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const bool scaled = options_.bank == ExplainBank::haar && !options_.summing;
    const auto title = tr("tui.explain.matrix") + (scaled ? tr("tui.explain.matrix_scale") : std::string{});
    Elements out;
    if (w.a1.size() == 0) {
        out.push_back(text(tr("tui.explain.no_matrix")) | fg(th.dim, ui_));
        return frame_panel(matrix, vbox(std::move(out)), {{"[L; B]", false}}, title);
    }
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    bool block_diagonal = true;
    const auto rows = static_cast<std::size_t>(w.a1.rows()), cols = static_cast<std::size_t>(w.a1.cols());
    for (std::size_t r = 0; r < rows; ++r) {
        Elements parts{text(r == 0 ? "L " : r == rows / 2 ? "B " : "  ") | fg(th.dim, ui_)};
        const auto k = r % (rows / 2);
        for (std::size_t c = 0; c < cols; ++c) {
            const double v = w.a1(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c));
            if (std::abs(v) < 1e-12) {
                parts.push_back(text(ascii ? " ." : " ·") | fg(th.dim, ui_));
                continue;
            }
            if (c != 2 * k && c != 2 * k + 1) block_diagonal = false;
            parts.push_back(text(v > 0 ? " +" : (ascii ? " -" : " −")) | fg(v > 0 ? th.trace[0] : th.trace[1], ui_));
        }
        parts.push_back(text("  → " + std::string{r < rows / 2 ? "a" : "d"} + subscript_count(k)) | fg(th.dim, ui_));
        out.push_back(hbox(std::move(parts)));
    }
    const auto inv = check_invariant("orthogonal_matrix", w);
    const std::string mark{glyph(inv.passed ? Glyph::check : Glyph::cross, ui_.caps.glyphs)};
    out.push_back(text(""));
    if (w.orthogonal)
        out.push_back(hbox({text(tr("tui.explain.ata") + " "), text(mark) | fg(inv.passed ? th.ok : th.err, ui_), text("   " + tr("tui.explain.s_is_at"))}));
    else
        out.push_back(hbox({text(tr("tui.explain.sa") + " "), text(mark) | fg(inv.passed ? th.ok : th.err, ui_)}));
    out.push_back(text(block_diagonal ? tr("tui.explain.block_diagonal")
                                      : fmt::format(fmt::runtime(tr("tui.explain.band_width")), fmt::arg("width", w.bank.length()))) |
                  fg(th.dim, ui_));
    return frame_panel(matrix, vbox(std::move(out)), {{"[L; B]", false}}, title);
}

ftxui::Element ExplainScreen::freq_panel(const Walkthrough& w) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto& zoom = zoom_.axis("freq", {0, std::numbers::pi, AxisScale::pi});
    PlotSpec spec;
    spec.x = zoom.axis();
    double peak = 0;
    for (double m : w.h0_response.magnitude) peak = std::max(peak, m);
    for (double m : w.h1_response.magnitude) peak = std::max(peak, m);
    spec.y = {0, std::max(peak, 1e-9) * 1.08};
    auto omega = [&](const std::vector<double>& v) {
        std::vector<double> out;
        out.reserve(v.size());
        for (double x : v) out.push_back(zoom.plot_x(x));
        return out;
    };
    spec.traces.push_back({w.h1_response.magnitude, omega(w.h1_response.omega), th.trace[1]});
    spec.traces.push_back({w.h0_response.magnitude, omega(w.h0_response.omega), th.trace[0]});
    const auto mid0 = nearest(w.h0_response.omega, std::numbers::pi / 2), mid1 = nearest(w.h1_response.omega, std::numbers::pi / 2);
    const double g0 = mid0 < w.h0_response.group_delay.size() ? w.h0_response.group_delay[mid0] : NAN;
    const double g1 = mid1 < w.h1_response.group_delay.size() ? w.h1_response.group_delay[mid1] : NAN;
    const auto gd = std::abs(g0 - g1) < 1e-6
                        ? fmt::format(fmt::runtime(tr("tui.explain.gd_both")), fmt::arg("value", vulgar(g0).empty() ? "1" : vulgar(g0)))
                        : fmt::format(fmt::runtime(tr("tui.explain.gd_each")), fmt::arg("h0", format_number(g0)), fmt::arg("h1", format_number(g1)));
    auto filled = [&](std::string_view s) {
        auto r = fill(s, w);
        return r ? *r : std::string{s};
    };
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs);
    auto body = vbox({zoom_.track("freq", std::move(spec), std::move(drawn), {tr("tui.explain.freq"), {"|H₀|", "|H₁|"}, "ω", ""}) | flex, text(gd) | fg(th.dim, ui_),
                      text(fmt::format(fmt::runtime(tr("tui.explain.cutoff")), fmt::arg("value", filled("{{cutoff}}")))) | fg(th.dim, ui_),
                      text(fmt::format(fmt::runtime(tr("tui.explain.leak")), fmt::arg("value", filled("{{leak}}")))) | fg(th.dim, ui_)});
    return frame_panel(freq, body, {}, tr("tui.explain.freq"));
}

ftxui::Element ExplainScreen::p0_panel(const Walkthrough& w) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto ok = std::string{glyph(Glyph::check, ui_.caps.glyphs)}, bad = std::string{glyph(Glyph::cross, ui_.caps.glyphs)};
    Elements out;
    out.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.explain.p0_formula")), fmt::arg("value", p0_text(w.bank)))) | fg(th.fg, ui_));
    out.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.explain.p0_diff")), fmt::arg("power", power_text(w.delay)))) | fg(th.fg, ui_));
    out.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.explain.delay")), fmt::arg("value", w.delay))) | fg(th.dim, ui_));
    const auto product = core::trim(core::multiply(w.bank.f0, w.bank.h0), 1e-12);
    out.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.explain.zeros")), fmt::arg("count", core::zeros_at_minus_one(product, 1e-6)))) | fg(th.dim, ui_));
    std::string rule;
    for (int i = 0; i < 28; ++i) rule += glyph(Glyph::h, ui_.caps.glyphs);
    out.push_back(text(rule) | fg(th.dim, ui_));
    const auto t = core::as_monomial(core::distortion_transfer(w.bank), 1e-9);
    std::string t_value = bad;
    if (t) t_value = (std::abs(t->coef - 1) < 1e-9 ? std::string{} : format_number(t->coef) + "·") + "z" + power_text(t->exponent) + " " + ok;
    out.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.explain.t_line")), fmt::arg("value", t_value))) | fg(t ? th.fg : th.err, ui_));
    out.push_back(text(tr("tui.explain.a_line")) | fg(th.fg, ui_));
    const auto alias = check_invariant("alias_cancelled", w);
    if (alias.passed)
        out.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.explain.a_zero")), fmt::arg("mark", ok))) | fg(th.ok, ui_));
    else
        out.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.explain.a_max")), fmt::arg("db", db_text(w.max_alias_db)), fmt::arg("mark", bad))) | fg(th.err, ui_));
    out.push_back(paragraph(fmt::format(fmt::runtime(tr(step_ == 12 ? "tui.explain.note_now" : "tui.explain.note_later")),
                                   fmt::arg("db", db_text(w.branch_alias_db)))) |
                  fg(step_ == 12 ? th.warn : th.dim, ui_));
    out.push_back(text(""));
    out.push_back(hbox({key_label(options_.perturb_f1 ? " E " : " e ", ui_),
                        text(" " + tr(options_.perturb_f1 ? "tui.explain.restore" : "tui.explain.perturb")) | fg(th.dim, ui_)}));
    return frame_panel(Panel::p0, vbox(std::move(out)), {}, tr("tui.explain.p0"));
}

ftxui::Element ExplainScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    notes_.clear();
    zoom_.begin_frame();
    step_boxes_.fill(kNoBox);
    bank_boxes_.fill(kNoBox);
    prev_box_ = next_box_ = auto_box_ = kNoBox;
    if (auto_) {
        const auto t = now();
        if (t - last_advance_ >= kAutoStep) {
            last_advance_ = t;
            if (step_ < 12) set_step(step_ + 1);
            if (step_ == 12) auto_ = false;
        }
    }
    if (ui_.ticker) ui_.ticker->want("explain", auto_);
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const auto* w = current();
    if (!w) return frame_panel(scheme, text(error_.value_or("")) | fg(ui_.theme.err, ui_), {}, tr("tui.sub.explain"));
    if (breakpoint_ <= Breakpoint::small || maximized_) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case scheme: return scheme_panel(*w, cols, false);
        case steps: return frame_panel(steps, steps_body(), {}, tr("tui.explain.steps_title"));
        case story: return text_panel(*w);
        case matrix: return matrix_panel(*w);
        case freq: return freq_panel(*w);
        case p0: return p0_panel(*w);
        case panel_count: break;
        }
    }
    const bool wide = cols >= 150;
    const int text_w = wide ? 46 : 38, matrix_w = wide ? 38 : 30, p0_w = wide ? 34 : 32;
    auto top = scheme_panel(*w, cols, true) | size(HEIGHT, EQUAL, 21);
    auto bottom = hbox({text_panel(*w) | size(WIDTH, EQUAL, text_w), text(" "), matrix_panel(*w) | size(WIDTH, EQUAL, matrix_w), text(" "),
                        freq_panel(*w) | flex, text(" "), p0_panel(*w) | size(WIDTH, EQUAL, p0_w)});
    return vbox({top, text(""), bottom | flex});
}

bool ExplainScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        if (zoom_.on_mouse(m)) return true;
        if (m.button != ftxui::Mouse::Left || m.motion != ftxui::Mouse::Pressed) return false;
        for (std::size_t i = 0; i < step_boxes_.size(); ++i)
            if (step_boxes_[i].Contain(m.x, m.y)) {
                set_step(i + 1);
                return true;
            }
        if (prev_box_.Contain(m.x, m.y)) return set_step(step_ - 1), true;
        if (next_box_.Contain(m.x, m.y)) return set_step(step_ + 1), true;
        if (auto_box_.Contain(m.x, m.y)) {
            auto_ = !auto_;
            last_advance_ = now();
            return true;
        }
        const ExplainBank banks[]{ExplainBank::haar, ExplainBank::db2, ExplainBank::legall53, ExplainBank::user};
        for (std::size_t i = 0; i < 4; ++i)
            if (bank_boxes_[i].Contain(m.x, m.y)) return set_bank(banks[i]), true;
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) {
                focus_ = static_cast<Panel>(p);
                zoom_.press(m.x, m.y);
                return true;
            }
        return false;
    }
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
        return true;
    }
    if (e == Event::Escape && maximized_) {
        maximized_.reset();
        return true;
    }
    if (focus_ == freq && zoom_.on_key(e, "freq")) return true;
    if (focus_ == story) {
        const auto scroll = [this](float d) { story_scroll_ = std::clamp(story_scroll_ + d, 0.f, 1.f); };
        if (e == Event::ArrowDown) return scroll(0.15f), true;
        if (e == Event::ArrowUp) return scroll(-0.15f), true;
        if (e == Event::PageDown) return scroll(0.5f), true;
        if (e == Event::PageUp) return scroll(-0.5f), true;
    }
    if (e == Event::ArrowRight || e == Event::ArrowDown) return set_step(step_ + 1), true;
    if (e == Event::ArrowLeft || e == Event::ArrowUp) return set_step(step_ - 1), true;
    if (e == Event::PageDown) return set_step(std::min<std::size_t>(12, step_ + 3)), true;
    if (e == Event::PageUp) return set_step(step_ > 3 ? step_ - 3 : 1), true;
    if (e == Event::Home) return set_step(1), true;
    if (e == Event::End) return set_step(12), true;
    if (e == Event::Character(' ')) {
        auto_ = !auto_;
        last_advance_ = now();
        return true;
    }
    if (e == Event::Character('w')) return cycle_bank(), true;
    if (e == Event::Character('p')) {
        options_.odd_phase = !options_.odd_phase;
        dirty_ = true;
        return true;
    }
    if (e == Event::Character('e') || e == Event::Character('E')) {
        options_.perturb_f1 = e == Event::Character('e');
        dirty_ = true;
        return true;
    }
    if (e == Event::Character('f')) {
        options_.summing = !options_.summing;
        dirty_ = true;
        return true;
    }
    if (e == Event::Return) {
        const auto& view = haar_script()[step_ - 1].view;
        if (!view.empty() && ui_.navigate) ui_.navigate(view, NavArgs{.wavelet = wavelet_name(), .n = 8});
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse) {
        const bool back = e == Event::TabReverse;
        const std::size_t step = back ? panel_count - 1 : 1;
        if (maximized_) maximized_ = focus_ = static_cast<Panel>((*maximized_ + step) % panel_count);
        else if (breakpoint_ <= Breakpoint::small) layout_.next_panel(panel_count, back);
        else focus_ = static_cast<Panel>((focus_ + step) % panel_count);
        return true;
    }
    return false;
}

Axis ExplainScreen::plot_axis(std::string_view id) const { return zoom_.view(id).value_or(Axis{}); }

std::vector<MenuItem> ExplainScreen::context_menu(int x, int y) {
    if (auto items = zoom_.menu(x, y, ui_); !items.empty()) return items;
    return {};
}

ftxui::Box ExplainScreen::area(std::string_view id) const {
    if (id == "freq") return zoom_.box(id);
    if (id.starts_with("step_")) {
        const auto n = static_cast<std::size_t>(std::atoi(std::string{id.substr(5)}.c_str()));
        return n >= 1 && n <= step_boxes_.size() ? step_boxes_[n - 1] : kNoBox;
    }
    if (id == "prev") return prev_box_;
    if (id == "next") return next_box_;
    if (id == "auto") return auto_box_;
    return kNoBox;
}

std::vector<JumpTarget> ExplainScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (std::size_t i = 0; i < step_boxes_.size(); ++i) out.push_back({step_boxes_[i], std::to_string(i + 1)});
    out.push_back({prev_box_, "prev"});
    out.push_back({next_box_, "next"});
    out.push_back({auto_box_, "auto"});
    for (const auto& b : bank_boxes_) out.push_back({b, "bank"});
    return out;
}

std::optional<std::string> ExplainScreen::explain_at(int x, int y) const {
    auto in = [&](const ftxui::Box& b) { return valid(b) && b.Contain(x, y); };
    for (const auto& b : step_boxes_)
        if (in(b)) return notes_.explain("step");
    if (in(prev_box_)) return notes_.explain("prev");
    if (in(next_box_)) return notes_.explain("next");
    if (in(auto_box_)) return notes_.explain("auto");
    static constexpr const char* kBanks[]{"bank.haar", "bank.db2", "bank.legall53", "bank.user"};
    for (std::size_t i = 0; i < bank_boxes_.size(); ++i)
        if (in(bank_boxes_[i])) return notes_.explain(kBanks[i]);
    return notes_.at(x, y);
}

std::vector<Command> ExplainScreen::commands() {
    return {{"explain.haar", tr("tui.explain.cmd.haar"), [this] { set_bank(ExplainBank::haar); }},
            {"explain.db2", tr("tui.explain.cmd.db2"), [this] { set_bank(ExplainBank::db2); }},
            {"explain.53", tr("tui.explain.cmd.legall53"), [this] { set_bank(ExplainBank::legall53); }}};
}

std::optional<std::pair<int, int>> ExplainScreen::menu_anchor() const {
    const auto& b = panel_boxes_[static_cast<std::size_t>(focus_)];
    if (b.x_max < b.x_min) return std::nullopt;
    return std::pair{(b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2};
}

std::shared_ptr<TabScreen> make_explain_screen(UiContext& ui, std::function<std::optional<core::FilterBank>()> user_bank) {
    return std::make_shared<ExplainScreen>(ui, std::move(user_bank));
}

}  // namespace signals::tui
