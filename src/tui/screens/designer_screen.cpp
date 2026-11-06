#include "tui/screens/designer_screen.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "i18n/i18n.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kLeftWide = 47, kLeftNarrow = 40;

std::string signed_text(double v) { return (v < 0 ? "−" : "+") + format_fixed(std::abs(v), 4); }

std::string coefficients(const core::Laurent& h) {
    std::string out;
    for (double c : core::trim(h, 0.0).coeffs) out += (out.empty() ? "" : " ") + signed_text(c);
    return out;
}

std::string clip_text(const std::string& s, int width) {
    const auto cps = split_codepoints(s);
    if (static_cast<int>(cps.size()) <= width) return s;
    std::string out;
    for (int i = 0; i + 1 < width; ++i) out += cps[static_cast<std::size_t>(i)];
    return out + "…";
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
    const bool few = n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14);
    return fmt::format(fmt::runtime(tr(few ? "tui.designer.phi_few" : "tui.designer.phi_many")), fmt::arg("n", n));
}

}  // namespace

DesignerScreen::DesignerScreen(UiContext& ui, std::shared_ptr<DesignerModel> model) : ui_{ui}, model_{std::move(model)} {
    panel_boxes_.fill(kNoBox);
    roots_box_ = kNoBox;
}

std::string DesignerScreen::tab_label() const { return tr("tui.sub.designer"); }

std::vector<Hint> DesignerScreen::hints() const {
    return {{tr("tui.signal.key.click"), tr("tui.designer.hint.click")}, {"←→", tr("tui.designer.hint.zeros")},
            {"n/N", tr("tui.designer.hint.next")},                        {"+/−", "p"},
            {"c", tr("tui.designer.hint.cascade")},                       {"x", tr("tui.designer.hint.swap")},
            {"Enter", tr("tui.designer.hint.save")},                      {"Tab", tr("tui.explain.hint.tab")}};
}

std::string DesignerScreen::context() const {
    const int p = model_->p();
    return fmt::format(fmt::runtime(tr("tui.designer.context")), fmt::arg("degree", 4 * p - 2), fmt::arg("p", p));
}

ftxui::Element DesignerScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1),
                                     fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
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
    }
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    auto legend = vbox({text(std::string{ascii ? "* " : "● "} + tr("tui.designer.in_h0")) | fg(th.trace[0], ui_),
                        text(std::string{ascii ? "o " : "○ "} + tr("tui.designer.in_f0")) | fg(f0_color, ui_),
                        text(std::string{ascii ? ". " : "· "} + "|z| = 1") | fg(th.dim, ui_)});
    auto body = hbox({zplane(spec, th, ui_.caps.color, ui_.caps.glyphs) | reflect(roots_box_) | flex, legend | size(WIDTH, EQUAL, 11)});
    if (!d) body = centered({text(tr("tui.designer.computing")) | fg(th.dim, ui_)});
    return frame_panel(roots, body, {}, tr("tui.designer.roots"));
}

ftxui::Element DesignerScreen::splits_panel(const DesignerData* d) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto f0_color = tint(th.trace[0], th.fg, 0.5);
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::string on_box = ascii ? "[x] " : "☑ ", off_box = ascii ? "[ ] " : "☐ ";
    if (!d) return frame_panel(splits, text(tr("tui.designer.computing")) | fg(th.dim, ui_), {}, tr("tui.designer.splits"));
    const auto& s = model_->split();
    auto& jobs = *ui_.jobs;
    Elements rows{text(fmt::format(fmt::runtime(tr("tui.designer.count")), fmt::arg("n", d->rows.size()), fmt::arg("w", d->wavelet_rows))) |
                      fg(th.dim, ui_),
                  text(tr("tui.designer.header")) | fg(th.dim, ui_)};
    const std::string full = ascii ? "*" : "●", hollow = ascii ? "o" : "○";
    std::string dots_h, dots_f;
    for (int i = 0; i < s.zeros_h0; ++i) dots_h += full;
    for (int i = 0; i < 2 * d->p - s.zeros_h0; ++i) dots_f += hollow;
    rows.push_back(hbox({text(pad_right("z = −1", 20) + pad_right(std::to_string(2 * d->p), 5)), text(pad_right(dots_h, 10)) | fg(th.trace[0], ui_),
                         text(pad_right(dots_f, 8)) | fg(f0_color, ui_),
                         clickable("zeros_less", text(" ◂ ") | fg(th.accent, ui_), [this, &jobs] { model_->zeros(-1, jobs); }),
                         clickable("zeros_more", text(" ▸ ") | fg(th.accent, ui_), [this, &jobs] { model_->zeros(1, jobs); })}));
    for (std::size_t g = 0; g < d->f.groups.size(); ++g) {
        const auto& grp = d->f.groups[g];
        const bool in_h0 = g < s.group_in_h0.size() && s.group_in_h0[g];
        const int count = grp.conjugate_pair ? 2 : 1;
        std::string marks;
        for (int i = 0; i < count; ++i) marks += in_h0 ? full : hollow;
        auto where = in_h0 ? text(marks + " " + tr("tui.designer.in_h0")) | fg(th.trace[0], ui_)
                           : text(std::string(10, ' ') + marks + " " + tr("tui.designer.in_f0")) | fg(f0_color, ui_);
        rows.push_back(clickable("group_" + std::to_string(g),
                                 hbox({text(pad_right(root_text(grp), 20) + pad_right(std::to_string(count), 5)), where}),
                                 [this, g, &jobs] { model_->toggle_group(g, jobs); }));
    }
    std::string rule;
    for (int i = 0; i < 42; ++i) rule += glyph(Glyph::h, ui_.caps.glyphs);
    rows.push_back(text(rule) | fg(th.dim, ui_));
    Elements presets{text(tr("tui.designer.presets") + " ")};
    const std::pair<Preset, const char*> all[]{{Preset::daubechies, "daubechies"}, {Preset::max_phase, "max_phase"},
                                               {Preset::cdf, "cdf"},               {Preset::degenerate, "degenerate"}};
    for (const auto& [preset, key] : all) {
        const bool on = same_split(s, preset_split(d->f, preset));
        const auto label = tr(std::string{"tui.designer.preset."} + key);
        presets.push_back(clickable(std::string{"preset_"} + key, text(on ? "[" + label + "]" : " " + label + " ") | fg(on ? th.accent : th.fg, ui_),
                                    [this, preset, &jobs] { model_->preset(preset, jobs); }));
        presets.push_back(text(" "));
    }
    rows.push_back(hbox(std::move(presets)));
    const auto c = model_->constraints();
    rows.push_back(text(on_box + tr("tui.designer.conjugates")) | fg(th.dim, ui_));
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

ftxui::Element DesignerScreen::result_panel(const DesignerResult* r, int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!r) return frame_panel(result, text(tr("tui.designer.computing")) | fg(th.dim, ui_), {}, tr("tui.designer.result_empty"));
    const auto ok = std::string{glyph(Glyph::check, ui_.caps.glyphs)}, bad = std::string{glyph(Glyph::cross, ui_.caps.glyphs)};
    const int room = std::max(10, width - 8);
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
    Elements rows{hbox({text("h₀ ") | fg(th.trace[0], ui_), text(clip_text(coefficients(h0), room)) | fg(th.fg, ui_)}),
                  hbox({text("f₀ ") | fg(tint(th.trace[0], th.fg, 0.5), ui_), text(clip_text(coefficients(f0), room)) | fg(th.fg, ui_)}),
                  text(tr("tui.designer.convention")) | fg(th.dim, ui_), line(checks, ui_), text(note) | fg(th.dim, ui_)};
    if (!notice_.empty()) rows.push_back(text(notice_) | fg(th.accent, ui_));
    const auto chip = fmt::format(fmt::runtime(tr("tui.designer.lengths")), fmt::arg("delay", r->delay ? std::to_string(*r->delay) : "—"),
                                  fmt::arg("h", h0.coeffs.size()), fmt::arg("f", f0.coeffs.size()));
    return frame_panel(result, vbox(std::move(rows)), {{chip, false}}, tr("tui.designer.result") + " · " + r->title);
}

ftxui::Element DesignerScreen::magnitude_panel(const DesignerResult* r) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    PlotSpec spec;
    spec.x = {0, std::numbers::pi, AxisScale::pi};
    spec.y = {-80, 0};
    if (r) {
        auto add = [&](const core::FrequencyResponse& resp, Rgb color, LineStyle style) {
            double peak = -1e300;
            for (double v : resp.magnitude_db) peak = std::max(peak, v);
            tui::Trace t;
            t.x = resp.omega;
            for (double v : resp.magnitude_db) t.y.push_back(std::max(-81.0, v - peak));
            t.color = color;
            t.style = style;
            spec.traces.push_back(std::move(t));
        };
        add(r->f0_response, tint(th.trace[0], th.fg, 0.5), LineStyle::dashed);
        add(r->h0_response, th.trace[0], LineStyle::solid);
    }
    return frame_panel(magnitude, plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs), {{tr("tui.designer.same_view"), false}},
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
        spec.x = {0, std::max(1.0, c.support)};
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
    auto body = vbox({plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | flex, text(status) | fg(ok ? th.ok : th.err, ui_)});
    return frame_panel(id, body, std::move(chips), iterations_title(iterations, dual));
}

ftxui::Element DesignerScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    clickables_.clear();
    roots_box_ = kNoBox;
    if (ui_.jobs) model_->sync(*ui_.jobs);
    const auto d = model_->data();
    const auto r = model_->result();
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const int height = std::max(3, rows - 3);
    if (!ui_.jobs) return text("");
    if (breakpoint_ <= Breakpoint::small) {
        const auto p = static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case roots: return roots_panel(d.get(), r.get());
        case splits: return splits_panel(d.get());
        case result: return result_panel(r.get(), cols);
        case magnitude: return magnitude_panel(r.get());
        case phi: return cascade_panel(r.get(), false);
        case phi_dual: return cascade_panel(r.get(), true);
        case panel_count: break;
        }
    }
    const int left = cols >= 150 ? kLeftWide : kLeftNarrow;
    const int roots_rows = height / 2 - 1;
    auto left_column = vbox({roots_panel(d.get(), r.get()) | size(HEIGHT, EQUAL, roots_rows), text(""), splits_panel(d.get()) | flex});
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
    const int p = model_->p();
    const std::string initial = "p" + std::to_string(p) + "-" +
                                (r->title.starts_with("db") ? "db" + std::to_string(p) : "k" + std::to_string(model_->split().zeros_h0));
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
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) {
                focus_ = static_cast<Panel>(p);
                return true;
            }
        return false;
    }
    if (e == Event::ArrowRight) return model_->zeros(1, jobs), true;
    if (e == Event::ArrowLeft) return model_->zeros(-1, jobs), true;
    if (e == Event::Character('n')) return model_->step(1, jobs), true;
    if (e == Event::Character('N')) return model_->step(-1, jobs), true;
    if (e == Event::Character('+') || e == Event::Character('=')) return model_->set_p(model_->p() + 1, jobs), true;
    if (e == Event::Character('-')) return model_->set_p(model_->p() - 1, jobs), true;
    if (e == Event::Character('x')) return model_->swap(jobs), true;
    if (e == Event::Character('c')) {
        const int next = model_->cascade_iterations() + 1;
        model_->set_cascade_iterations(next > DesignerModel::kMaxIterations ? DesignerModel::kMinIterations : next);
        return true;
    }
    if (e == Event::Return) return save(), true;
    if (e == Event::Tab) {
        if (breakpoint_ <= Breakpoint::small) layout_.next_panel(panel_count);
        else focus_ = static_cast<Panel>((focus_ + 1) % panel_count);
        return true;
    }
    return false;
}

ftxui::Box DesignerScreen::area(std::string_view id) const {
    if (id == "roots") return roots_box_;
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

std::vector<JumpTarget> DesignerScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
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

std::vector<MenuItem> DesignerScreen::context_menu(int x, int y) {
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

std::shared_ptr<TabScreen> make_designer_screen(UiContext& ui, std::shared_ptr<DesignerModel> model) {
    return std::make_shared<DesignerScreen>(ui, std::move(model));
}

}  // namespace signals::tui
