#include "tui/screens/designer_screen.hpp"

#include <algorithm>
#include <complex>
#include <cmath>
#include <numbers>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "i18n/i18n.hpp"
#include "tui/screens/publish_util.hpp"
#include "tui/shell/plot_export.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/list_window.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kLeft = 47;

std::string signed_text(double v) { return (v < 0 ? "−" : "+") + format_fixed(std::abs(v), 4); }

std::string coefficients(const core::Laurent& h) {
    std::string out;
    for (double c : core::trim(h, 0.0).coeffs) out += (out.empty() ? "" : " ") + signed_text(c);
    return out;
}

std::string root_text(const core::RootGroup& g) {
    const double re = static_cast<double>(g.root.re), im = static_cast<double>(g.root.im);
    std::string s = format_fixed(re, 3);
    if (g.conjugate_pair) s += " ± " + format_fixed(std::abs(im), 3) + "j";
    return s;
}

bool converges(double sobolev) { return std::isfinite(sobolev) && sobolev > 1e-6; }

std::string iterations_title(int n, bool dual) {
    if (dual) return tr("tui.designer.phi_dual");
    return fmt::format(fmt::runtime(tr("tui.designer.phi_many")), fmt::arg("n", tr_count("unit.iteration", n)));
}

}  // namespace

DesignerScreen::DesignerScreen(UiContext& ui, std::shared_ptr<DesignerModel> model) : ui_{ui}, model_{std::move(model)} {
    panel_boxes_.fill(kNoBox);
    roots_box_ = kNoBox;
}

std::optional<TheoryLink> DesignerScreen::theory_link() const { return TheoryLink{TheoryLink::Kind::card, "halfband"}; }

void DesignerScreen::publish(theory::LiveValues& v) const {
    const auto d = model_->data();
    const auto r = model_->result();
    const int p = model_->p();
    v.source = tab_label() + " ▸ p = " + std::to_string(p) + (r ? " · " + r->title : std::string{});
    v.set("block.p", std::to_string(p));
    v.set("block.degree", std::to_string(4 * p - 2));
    if (d) {
        v.set("block.splits", std::to_string(d->rows.size()));
        v.set("block.wavelet_splits", std::to_string(d->wavelet_rows));
    }
    if (!r) return;
    const auto h0 = core::trim(r->bank.h0, 0.0).coeffs.size(), f0 = core::trim(r->bank.f0, 0.0).coeffs.size();
    v.set("block.split", r->title);
    v.set("block.zeros_h0", std::to_string(r->split.zeros_h0));
    v.set("block.zeros_f0", std::to_string(2 * r->p - r->split.zeros_h0));
    v.set("block.h0_length", std::to_string(h0));
    v.set("block.f0_length", std::to_string(f0));
    v.set("block.delay", r->delay ? std::to_string(*r->delay) : std::string{"—"});
    v.set_check(renamed(r->pr.alias, "check.pr_alias"));
    v.set_check(renamed(r->pr.distortion, "check.pr_distortion"));
    v.set_check(renamed(core::check_orthogonality(r->bank, 1e-9), "check.orthogonality"));
    v.set("metric.sobolev_h0", format_fixed(r->info.sobolev_h0, 3));
    v.set("metric.sobolev_f0", format_fixed(r->info.sobolev_f0, 3));
    v.set("block.bank", r->title);
    v.set("block.length", std::to_string(h0) + " / " + std::to_string(f0));
    v.set("block.linear_phase", yes_no(r->linear_phase));
}

std::string DesignerScreen::tab_label() const { return tr("tui.sub.designer"); }

std::vector<Hint> DesignerScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},           {"z", tr("tui.signal.hint.zoom")},
            {"←→", tr("tui.designer.hint.zeros")},        {"n/N", tr("tui.designer.hint.next")},
            {"↑↓ PgUp PgDn", tr("tui.designer.hint.group")}, {"Space", tr("tui.designer.hint.toggle")},
            {"l o w", tr("tui.designer.hint.constraints")}, {"r", tr("tui.designer.hint.preset")},
            {"p/P", "p"},                                 {"+ − 0", tr("tui.hint.plot_zoom")},
            {"< >", tr("tui.hint.plot_pan")},             {"c", tr("tui.designer.hint.cascade")},
            {"x", tr("tui.designer.hint.swap")},          {"Enter", tr("tui.designer.hint.save")},
            {"F1", tr("tui.designer.hint.f1")},           {tr("tui.signal.key.click"), tr("tui.designer.hint.click")},
            {tr("tui.signal.key.wheel"), tr("tui.zoom.hint")}};
}

std::string DesignerScreen::context() const {
    const int p = model_->p();
    return fmt::format(fmt::runtime(tr("tui.designer.context")), fmt::arg("degree", 4 * p - 2), fmt::arg("p", p));
}

ftxui::Element DesignerScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    static constexpr const char* kNames[]{"roots", "splits", "result", "magnitude", "phi", "phi_dual"};
    const std::string name = kNames[static_cast<std::size_t>(p)];
    for (auto& c : chips) c.box = notes_.box("legend." + name);
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1),
                                     fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_, notes_.box("panel." + name)) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element DesignerScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), kNoBox, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element DesignerScreen::roots_panel(const DesignerData* d, const DesignerResult*) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto f0_color = tint(th.trace[0], th.fg, 0.5);
    ZPlaneSpec spec;
    spec.hits = hits_;
    if (d) {
        double far = 1;
        const auto& s = model_->split();
        spec.markers.push_back({{-1.0, 0.0}, "◉", th.trace[0], -1});
        spec.minus_one_label = fmt::format(fmt::runtime(tr("tui.designer.minus_one")), fmt::arg("h", s.zeros_h0), fmt::arg("f", 2 * d->p - s.zeros_h0));
        for (std::size_t g = 0; g < d->f.groups.size(); ++g) {
            const auto& grp = d->f.groups[g];
            const std::complex<double> z{static_cast<double>(grp.root.re), static_cast<double>(grp.root.im)};
            far = std::max(far, std::abs(z));
            const bool in_h0 = g < s.group_in_h0.size() && s.group_in_h0[g];
            spec.markers.push_back({z, in_h0 ? "●" : "○", in_h0 ? th.trace[0] : f0_color, static_cast<int>(g)});
            if (grp.conjugate_pair) spec.markers.push_back({std::conj(z), in_h0 ? "●" : "○", in_h0 ? th.trace[0] : f0_color, static_cast<int>(g)});
        }
        spec.extent = std::max(1.5, 1.1 * far);
        zview_.apply(spec);
        shown_roots_ = zplane_export("roots", tr("tui.export.roots"), spec, th,
                                     {{"●", tr("tui.designer.in_h0")}, {"○", tr("tui.designer.in_f0")}, {"◉", "z = −1"}});
    }
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    auto legend = vbox({text(std::string{ascii ? "* " : "● "} + tr("tui.designer.in_h0")) | fg(th.trace[0], ui_),
                        text(std::string{ascii ? "o " : "○ "} + tr("tui.designer.in_f0")) | fg(f0_color, ui_),
                        text(std::string{ascii ? ". " : "· "} + "|z| = 1") | fg(th.dim, ui_)});
    auto body = hbox({zplane(spec, th, ui_.caps.color, ui_.caps.glyphs) | reflect(roots_box_) | flex, legend | size(WIDTH, EQUAL, 11)});
    if (!d) body = centered({text(tr("tui.designer.computing")) | fg(th.dim, ui_)});
    return frame_panel(roots, body, {}, tr("tui.designer.roots"));
}

ftxui::Element DesignerScreen::splits_panel(const DesignerData* d, int max_rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto f0_color = tint(th.trace[0], th.fg, 0.5);
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::string on_box = ascii ? "[x] " : "☑ ", off_box = ascii ? "[ ] " : "☐ ";
    if (!d) return frame_panel(splits, text(tr("tui.designer.computing")) | fg(th.dim, ui_), {}, tr("tui.designer.splits"));
    const auto& s = model_->split();
    auto& jobs = *ui_.jobs;
    Elements rows{text(fmt::format(fmt::runtime(tr("tui.designer.count")), fmt::arg("n", tr_count("unit.variant", static_cast<long long>(d->rows.size()))), fmt::arg("w", d->wavelet_rows))) |
                      fg(th.dim, ui_),
                  notes_.mark("header", text(tr("tui.designer.header")) | fg(th.dim, ui_))};
    const std::string full = ascii ? "*" : "●", hollow = ascii ? "o" : "○";
    std::string dots_h, dots_f;
    for (int i = 0; i < s.zeros_h0; ++i) dots_h += full;
    for (int i = 0; i < 2 * d->p - s.zeros_h0; ++i) dots_f += hollow;
    rows.push_back(hbox({notes_.mark("zeros", text(pad_right("z = −1", 20) + pad_right(std::to_string(2 * d->p), 5))), text(pad_right(dots_h, 10)) | fg(th.trace[0], ui_),
                         text(pad_right(dots_f, 8)) | fg(f0_color, ui_),
                         clickable("zeros_less", text(" ◂ ") | fg(th.accent, ui_), [this, &jobs] { model_->zeros(-1, jobs); }),
                         clickable("zeros_more", text(" ▸ ") | fg(th.accent, ui_), [this, &jobs] { model_->zeros(1, jobs); })}));
    constexpr int kFixedRows = 10;
    const auto [first, shown] = list_window(groups_first_, d->f.groups.size(), std::max(3, max_rows - kFixedRows), 1,
                                             follow_group_ ? std::optional{group_cursor_} : std::nullopt);
    follow_group_ = false;
    const auto more = [&](bool up, std::size_t count) {
        return text(std::string{ascii ? (up ? "^ " : "v ") : (up ? "▲ " : "▼ ")} +
                    fmt::format(fmt::runtime(tr("tui.theory.blocks_more")), fmt::arg("count", count))) |
               fg(th.dim, ui_);
    };
    if (first > 0) rows.push_back(more(true, first));
    for (std::size_t g = first; g < first + shown; ++g) {
        const auto& grp = d->f.groups[g];
        const bool in_h0 = g < s.group_in_h0.size() && s.group_in_h0[g];
        const int count = grp.conjugate_pair ? 2 : 1;
        std::string marks;
        for (int i = 0; i < count; ++i) marks += in_h0 ? full : hollow;
        auto where = in_h0 ? text(marks + " " + tr("tui.designer.in_h0")) | fg(th.trace[0], ui_)
                           : text(std::string(10, ' ') + marks + " " + tr("tui.designer.in_f0")) | fg(f0_color, ui_);
        auto row = hbox({text(pad_right(root_text(grp), 20) + pad_right(std::to_string(count), 5)), where, filler()});
        if (g == group_cursor_ && focus_ == splits) row = row | bg(tint(th.bg, th.accent, 0.3), ui_);
        rows.push_back(clickable("group_" + std::to_string(g), row, [this, g, &jobs] {
            group_cursor_ = g;
            model_->toggle_group(g, jobs);
        }));
    }
    if (first + shown < d->f.groups.size()) rows.push_back(more(false, d->f.groups.size() - first - shown));
    std::string rule;
    for (int i = 0; i < 42; ++i) rule += glyph(Glyph::h, ui_.caps.glyphs);
    rows.push_back(text(rule) | fg(th.dim, ui_));
    Elements presets{notes_.mark("preset", text(tr("tui.designer.presets") + " "))};
    const std::pair<Preset, const char*> all[]{{Preset::daubechies, "daubechies"}, {Preset::max_phase, "max_phase"},
                                               {Preset::cdf, "cdf"},               {Preset::degenerate, "degenerate"}};
    for (const auto& [preset, key] : all) {
        const bool on = same_split(s, preset_split(d->f, preset));
        const auto label = tr(std::string{"tui.designer.preset."} + key);
        presets.push_back(clickable(std::string{"preset_"} + key, text(on ? "[" + label + "]" : " " + label + " ") | fg(on ? th.accent : th.fg, ui_),
                                    [this, preset, &jobs] { model_->preset(preset, jobs); }));
    }
    rows.push_back(hbox(std::move(presets)));
    const auto c = model_->constraints();
    rows.push_back(notes_.mark("conjugates", text(on_box + tr("tui.designer.conjugates")) | fg(th.dim, ui_)));
    rows.push_back(hbox({clickable("linear_phase", text((c.linear_phase ? on_box : off_box) + tr("tui.designer.linear_phase")) | fg(th.fg, ui_),
                                   [this, &jobs] {
                                       auto k = model_->constraints();
                                       k.linear_phase = !k.linear_phase;
                                       model_->set_constraints(k, jobs);
                                   }),
                         text("   "),
                         clickable("orthogonal", text((c.orthogonal ? on_box : off_box) + tr("tui.designer.orthogonal")) | fg(th.fg, ui_), [this, &jobs] {
                             auto k = model_->constraints();
                             k.orthogonal = !k.orthogonal;
                             model_->set_constraints(k, jobs);
                         })}));
    rows.push_back(clickable("wavelet_only", text((c.wavelet_only ? on_box : off_box) + tr("tui.designer.wavelet_only")) | fg(th.fg, ui_),
                             [this, &jobs] {
                                 auto k = model_->constraints();
                                 k.wavelet_only = !k.wavelet_only;
                                 model_->set_constraints(k, jobs);
                             }));
    rows.push_back(text(fmt::format(fmt::runtime(tr("tui.designer.visible")), fmt::arg("n", model_->visible_rows().size()),
                                    fmt::arg("total", d->rows.size()))) |
                   fg(th.dim, ui_));
    rows.push_back(hbox({key_label(" x ", ui_), text(" " + tr("tui.designer.hint.swap") + "  ") | fg(th.dim, ui_), key_label(" Enter ", ui_),
                         text(" " + tr("tui.designer.hint.save")) | fg(th.dim, ui_)}));
    std::vector<Chip> chips{{"p = " + std::to_string(d->p), false}};
    return frame_panel(splits, vbox(std::move(rows)), std::move(chips), tr("tui.designer.splits"));
}

ftxui::Element DesignerScreen::result_panel(const DesignerResult* r, int) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!r) return frame_panel(result, text(tr("tui.designer.computing")) | fg(th.dim, ui_), {}, tr("tui.designer.result_empty"));
    const auto ok = std::string{glyph(Glyph::check, ui_.caps.glyphs)}, bad = std::string{glyph(Glyph::cross, ui_.caps.glyphs)};
    const auto h0 = core::trim(r->bank.h0, 0.0), f0 = core::trim(r->bank.f0, 0.0);
    const double pr = std::max(r->pr.alias.value, r->pr.distortion.value);
    const bool pr_ok = r->pr.alias.passed() && r->pr.distortion.passed() && r->delay;
    const std::string pr_value = pr < 1e-15 ? "< 10⁻¹⁵" : format_number(pr, 2);
    const std::string delay = r->delay ? "⁻" + superscript_count(static_cast<int>(*r->delay)) : "";
    auto mark = [&](bool v) { return Span{v ? ok : bad, v ? th.ok : th.err}; };
    std::vector<Span> checks{{"PR: P₀(z) − P₀(−z) = 2z" + delay + " ", th.fg}, mark(pr_ok), {" " + pr_value + "   ", th.dim},
                             {tr("tui.designer.orth") + " ", th.fg}, mark(r->orthogonal), {"   " + tr("tui.designer.lp") + " ", th.fg},
                             mark(r->linear_phase), {"   " + tr("tui.designer.wavelet") + " ", th.fg}, mark(r->info.wavelet)};
    std::string note;
    if (r->info.wavelet) note = tr("tui.designer.note.wavelet");
    else note = fmt::format(fmt::runtime(tr("tui.designer.note.diverges")),
                            fmt::arg("filter", converges(r->info.sobolev_h0) ? "F₀" : "H₀"));
    Elements rows{hbox({text("h₀ ") | fg(th.trace[0], ui_), paragraph(coefficients(h0)) | fg(th.fg, ui_) | flex}),
                  hbox({text("f₀ ") | fg(tint(th.trace[0], th.fg, 0.5), ui_), paragraph(coefficients(f0)) | fg(th.fg, ui_) | flex}),
                  notes_.mark("convention", text(tr("tui.designer.convention")) | fg(th.dim, ui_)), notes_.mark("checks", line(checks, ui_)), text(note) | fg(th.dim, ui_)};
    if (!notice_.empty()) rows.push_back(paragraph(notice_) | fg(th.accent, ui_));
    const auto chip = fmt::format(fmt::runtime(tr("tui.designer.lengths")), fmt::arg("delay", r->delay ? std::to_string(*r->delay) : "—"),
                                  fmt::arg("h", h0.coeffs.size()), fmt::arg("f", f0.coeffs.size()));
    return frame_panel(result, vbox(std::move(rows)), {{chip, false}}, tr("tui.designer.result") + " · " + r->title);
}

ftxui::Element DesignerScreen::magnitude_panel(const DesignerResult* r) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto& zoom = zoom_.axis("magnitude", {0, std::numbers::pi, AxisScale::pi});
    PlotSpec spec;
    spec.x = zoom.axis();
    spec.y = {-80, 0};
    if (r) {
        auto add = [&](const core::FrequencyResponse& resp, Rgb color, LineStyle style) {
            double peak = -1e300;
            for (double v : resp.magnitude_db) peak = std::max(peak, v);
            tui::Trace t;
            for (double w : resp.omega) t.x.push_back(zoom.plot_x(w));
            for (double v : resp.magnitude_db) t.y.push_back(std::max(-81.0, v - peak));
            t.color = color;
            t.style = style;
            spec.traces.push_back(std::move(t));
        };
        add(r->f0_response, tint(th.trace[0], th.fg, 0.5), LineStyle::dashed);
        add(r->h0_response, th.trace[0], LineStyle::solid);
    }
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs);
    return frame_panel(magnitude, zoom_.track("magnitude", std::move(spec), std::move(drawn), {tr("tui.designer.magnitude"), {}, "ω", ""}),
                       {{tr("tui.designer.same_view"), false}},
                       tr("tui.designer.magnitude"));
}

ftxui::Element DesignerScreen::cascade_panel(const DesignerResult* r, bool dual) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const int iterations = model_->cascade_iterations();
    const Panel id = dual ? phi_dual : phi;
    if (!r) return frame_panel(id, text(""), {}, iterations_title(iterations, dual));
    const auto& steps = dual ? r->phi_dual_steps : r->phi_steps;
    const auto index = static_cast<std::size_t>(iterations - DesignerModel::kMinIterations);
    PlotSpec spec;
    std::vector<Chip> chips;
    if (index < steps.size()) {
        const auto& c = steps[index];
        double lo = -1, hi = 1.5;
        tui::Trace t;
        for (std::size_t k = 0; k < c.phi.size(); ++k) {
            t.x.push_back(static_cast<double>(k) * c.dt);
            t.y.push_back(c.phi[k]);
            lo = std::min(lo, c.phi[k]);
            hi = std::max(hi, c.phi[k]);
        }
        spec.x = zoom_.axis(dual ? "phi_dual" : "phi", {0, std::max(1.0, c.support)}).axis();
        spec.y = {std::max(lo, -3.0), std::min(hi, 3.0)};
        t.color = dual ? tint(th.trace[0], th.fg, 0.5) : th.trace[0];
        spec.traces.push_back(std::move(t));
    }
    const double sobolev = dual ? r->info.sobolev_f0 : r->info.sobolev_h0;
    const auto& last = dual ? r->phi_dual : r->phi;
    const bool ok = converges(sobolev);
    const auto status = ok ? tr("tui.designer.converges") + " " + std::string{glyph(Glyph::check, ui_.caps.glyphs)}
                           : fmt::format(fmt::runtime(tr("tui.designer.diverges")), fmt::arg("mark", std::string{glyph(Glyph::cross, ui_.caps.glyphs)}),
                                         fmt::arg("ratio", last ? format_fixed(last->norm_ratio, 2) : std::string{"—"}));
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs);
    auto body = vbox({zoom_.track(dual ? "phi_dual" : "phi", std::move(spec), std::move(drawn), {iterations_title(iterations, dual)}) | flex, text(status) | fg(ok ? th.ok : th.err, ui_)});
    return frame_panel(id, body, std::move(chips), iterations_title(iterations, dual));
}

ftxui::Element DesignerScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    clickables_.clear();
    notes_.clear();
    roots_box_ = kNoBox;
    shown_roots_.reset();
    zoom_.begin_frame();
    if (ui_.jobs) model_->sync(*ui_.jobs);
    const auto d = model_->data();
    const auto r = model_->result();
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const int height = std::max(3, rows - 3);
    if (!ui_.jobs) return text("");
    if (breakpoint_ <= Breakpoint::small || maximized_) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case roots: return roots_panel(d.get(), r.get());
        case splits: return splits_panel(d.get(), height - 2);
        case result: return result_panel(r.get(), cols);
        case magnitude: return magnitude_panel(r.get());
        case phi: return cascade_panel(r.get(), false);
        case phi_dual: return cascade_panel(r.get(), true);
        case panel_count: break;
        }
    }
    const int left = kLeft;
    const int roots_rows = height / 2 - 1;
    auto left_column = vbox({roots_panel(d.get(), r.get()) | size(HEIGHT, EQUAL, roots_rows), text(""), splits_panel(d.get(), height - roots_rows - 3) | flex});
    const int result_rows = notice_.empty() ? 7 : 8;
    const int magnitude_rows = std::max(8, (height - result_rows - 2) / 2);
    auto right_column = vbox({result_panel(r.get(), cols - left - 1) | size(HEIGHT, EQUAL, result_rows), text(""),
                              magnitude_panel(r.get()) | size(HEIGHT, EQUAL, magnitude_rows), text(""),
                              hbox({cascade_panel(r.get(), false) | flex, text(" "), cascade_panel(r.get(), true) | flex}) | flex});
    return hbox({left_column | size(WIDTH, EQUAL, left), text(" "), right_column | flex});
}

void DesignerScreen::save() {
    const auto r = model_->result();
    if (!r) return;
    const int p = r->p;
    const std::string initial = "p" + std::to_string(p) + "-" +
                                (r->title.starts_with("db") ? "db" + std::to_string(p) : "k" + std::to_string(r->split.zeros_h0));
    auto done = [this, r, p](std::string name) {
        if (name.empty()) return;
        UserBank b{name, r->bank.h0, r->bank.f0, "p = " + std::to_string(p) + ", " + r->title};
        const auto dir = ui_.user_filters.empty() ? user_filters_dir(process_env()) : ui_.user_filters;
        auto path = save_user_bank(dir, b);
        if (!path) {
            notice_ = path.error().message;
            return;
        }
        auto banks = user_banks();
        banks.push_back(std::move(b));
        set_user_banks(std::move(banks));
        notice_ = fmt::format(fmt::runtime(tr("tui.designer.saved")), fmt::arg("path", path->filename().string()));
    };
    if (ui_.ask_text) ui_.ask_text(tr("tui.designer.ask_name"), initial, std::move(done));
    else done(initial);
}

bool DesignerScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (!ui_.jobs) return false;
    auto& jobs = *ui_.jobs;
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        const double base = zplane_base(model_->data().get());
        const bool press = m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed;
        if (!press && zview_.on_mouse(m, roots_box_, base)) return true;
        if (m.button == ftxui::Mouse::WheelUp || m.button == ftxui::Mouse::WheelDown) {
            if (panel_boxes_[splits].Contain(m.x, m.y)) {
                if (m.button == ftxui::Mouse::WheelDown) ++groups_first_;
                else if (groups_first_ > 0) --groups_first_;
                return true;
            }
        }
        if (zoom_.on_mouse(m)) return true;
        if (m.button != ftxui::Mouse::Left || m.motion != ftxui::Mouse::Pressed) return false;
        for (auto& c : clickables_)
            if (c.box.Contain(m.x, m.y)) {
                auto action = c.action;
                action();
                return true;
            }
        if (hits_)
            for (const auto& h : *hits_)
                if (h.x == m.x && h.y == m.y && h.id >= 0) {
                    model_->toggle_group(static_cast<std::size_t>(h.id), jobs);
                    return true;
                }
        if (zview_.on_mouse(m, roots_box_, base)) return focus_ = roots, true;
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
    if (e == Event::ArrowRight) return model_->zeros(1, jobs), true;
    if (e == Event::ArrowLeft) return model_->zeros(-1, jobs), true;
    if (e == Event::Character('n')) return model_->step(1, jobs), true;
    if (e == Event::Character('N')) return model_->step(-1, jobs), true;
    if (const auto d = model_->data(); d && !d->f.groups.empty()) {
        const auto n = d->f.groups.size();
        group_cursor_ = std::min(group_cursor_, n - 1);
        if (e == Event::ArrowDown) return follow_group_ = true, group_cursor_ = std::min(group_cursor_ + 1, n - 1), focus_ = splits, true;
        if (e == Event::ArrowUp) return follow_group_ = true, group_cursor_ = group_cursor_ > 0 ? group_cursor_ - 1 : 0, focus_ = splits, true;
        if (e == Event::PageDown || e == Event::End) return follow_group_ = true, group_cursor_ = e == Event::End ? n - 1 : std::min(group_cursor_ + 5, n - 1), focus_ = splits, true;
        if (e == Event::PageUp || e == Event::Home) return follow_group_ = true, group_cursor_ = e == Event::Home ? 0 : group_cursor_ - std::min<std::size_t>(group_cursor_, 5), focus_ = splits, true;
        if (e == Event::Character(' ')) return model_->toggle_group(group_cursor_, jobs), true;
    }
    const auto flip = [&](bool Constraints::* field) {
        auto k = model_->constraints();
        k.*field = !(k.*field);
        model_->set_constraints(k, jobs);
        return true;
    };
    if (e == Event::Character('l')) return flip(&Constraints::linear_phase);
    if (e == Event::Character('o')) return flip(&Constraints::orthogonal);
    if (e == Event::Character('w')) return flip(&Constraints::wavelet_only);
    if (e == Event::Character('r')) {
        constexpr std::array presets{Preset::daubechies, Preset::max_phase, Preset::cdf, Preset::degenerate};
        preset_index_ = (preset_index_ + 1) % presets.size();
        model_->preset(presets[preset_index_], jobs);
        return true;
    }
    if (e == Event::Character('p')) return model_->set_p(model_->p() + 1, jobs), true;
    if (e == Event::Character('P')) return model_->set_p(model_->p() - 1, jobs), true;
    if (focus_ == magnitude && zoom_.on_key(e, "magnitude")) return true;
    if (focus_ == phi && zoom_.on_key(e, "phi")) return true;
    if (focus_ == phi_dual && zoom_.on_key(e, "phi_dual")) return true;
    if (focus_ == roots && zview_.on_key(e, zplane_base(model_->data().get()))) return true;
    if (e == Event::Character('x')) return model_->swap(jobs), true;
    if (e == Event::Character('c')) {
        const int next = model_->cascade_iterations() + 1;
        model_->set_cascade_iterations(next > DesignerModel::kMaxIterations ? DesignerModel::kMinIterations : next);
        return true;
    }
    if (e == Event::Return) return save(), true;
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

Axis DesignerScreen::plot_axis(std::string_view id) const {
    if (auto v = zoom_.view(id)) return *v;
    if (id == "roots") {
        const double ext = zview_.extent(zplane_base(model_->data().get()));
        return {zview_.center().real() - ext, zview_.center().real() + ext};
    }
    return {};
}

double DesignerScreen::zplane_base(const DesignerData* d) const {
    double far = 1;
    if (d)
        for (const auto& g : d->f.groups) far = std::max(far, std::abs(std::complex<double>{static_cast<double>(g.root.re), static_cast<double>(g.root.im)}));
    return std::max(1.5, 1.1 * far);
}

ftxui::Box DesignerScreen::area(std::string_view id) const {
    if (id == "roots") return roots_box_;
    if (id == "magnitude" || id == "phi" || id == "phi_dual") return zoom_.box(id);
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

std::vector<JumpTarget> DesignerScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
}

std::optional<std::string> DesignerScreen::explain_at(int x, int y) const {
    for (const auto& c : clickables_)
        if (valid(c.box) && c.box.Contain(x, y)) return notes_.explain(c.id);
    if (valid(roots_box_) && roots_box_.Contain(x, y)) return notes_.explain("roots");
    return notes_.at(x, y);
}

std::vector<Command> DesignerScreen::commands() {
    std::vector<Command> out;
    const std::pair<Preset, const char*> all[]{{Preset::daubechies, "daubechies"}, {Preset::max_phase, "max_phase"},
                                               {Preset::cdf, "cdf"},               {Preset::degenerate, "degenerate"}};
    for (const auto& [preset, key] : all)
        out.push_back({std::string{"designer."} + key, tr("tui.designer.cmd_prefix") + tr(std::string{"tui.designer.preset."} + key),
                       [this, preset] {
                           if (ui_.jobs) model_->preset(preset, *ui_.jobs);
                       }});
    return out;
}

std::vector<ChartExport> DesignerScreen::chart_exports() {
    if (shown_roots_) return {*shown_roots_};
    return {};
}

std::vector<MenuItem> DesignerScreen::context_menu(int x, int y) {
    if (auto items = zoom_.menu(x, y, ui_); !items.empty()) return items;
    if (roots_box_.Contain(x, y)) {
        std::vector<MenuItem> items;
        if (hits_)
            for (const auto& h : *hits_)
                if (h.x == x && h.y == y && h.id >= 0 && ui_.jobs) {
                    const auto g = static_cast<std::size_t>(h.id);
                    const bool in_h0 = g < model_->split().group_in_h0.size() && model_->split().group_in_h0[g];
                    items.push_back({tr(in_h0 ? "tui.designer.menu.to_f0" : "tui.designer.menu.to_h0"), tr("tui.signal.key.click"),
                                     [this, g] { model_->toggle_group(g, *ui_.jobs); }});
                }
        if (zview_.zoomed()) items.push_back({tr("tui.menu.reset_zoom"), "", [this] { zview_.reset(); }});
        if (shown_roots_) items.push_back(export_item(ui_, *shown_roots_));
        return items;
    }
    if (!hits_ || !ui_.jobs) return {};
    for (const auto& h : *hits_)
        if (h.x == x && h.y == y && h.id >= 0) {
            const auto g = static_cast<std::size_t>(h.id);
            const bool in_h0 = g < model_->split().group_in_h0.size() && model_->split().group_in_h0[g];
            return {{tr(in_h0 ? "tui.designer.menu.to_f0" : "tui.designer.menu.to_h0"), tr("tui.signal.key.click"),
                     [this, g] { model_->toggle_group(g, *ui_.jobs); }}};
        }
    return {};
}

std::optional<std::pair<int, int>> DesignerScreen::menu_anchor() const {
    const auto& b = panel_boxes_[static_cast<std::size_t>(focus_)];
    if (b.x_max < b.x_min) return std::nullopt;
    return std::pair{(b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2};
}

std::shared_ptr<TabScreen> make_designer_screen(UiContext& ui, std::shared_ptr<DesignerModel> model) {
    return std::make_shared<DesignerScreen>(ui, std::move(model));
}

}  // namespace signals::tui
