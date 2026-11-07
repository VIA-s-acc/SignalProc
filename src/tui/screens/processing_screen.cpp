#include "tui/screens/processing_screen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "i18n/i18n.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kRightWidth = 39;
constexpr int kBandLabel = 24;
constexpr std::array<double, 5> kScales{0.5, 0.75, 1.0, 1.5, 2.0};
constexpr std::array<const char*, 4> kPresets{"tui.processing.eq.flat", "tui.processing.eq.bass", "tui.processing.eq.no_d1", "tui.processing.eq.notch_d3"};

std::vector<double> preset_gains(std::size_t preset, std::size_t levels) {
    std::vector<double> g(levels + 1, 1.0);
    if (preset == 1) {
        g[0] = 2;
        if (levels >= 1) g[1] = 2;
        if (levels >= 2) g[2] = 1.4;
    } else if (preset == 2) {
        g[levels] = 0;
    } else if (preset == 3 && levels >= 3) {
        g[levels - 2] = 0;
    }
    return g;
}

std::vector<MinMaxPyramid::Range> columns_of(std::span<const double> v, int cols, double scale) {
    std::vector<MinMaxPyramid::Range> out(static_cast<std::size_t>(std::max(1, cols)));
    if (v.empty()) return out;
    for (std::size_t c = 0; c < out.size(); ++c) {
        const auto a = c * v.size() / out.size();
        const auto b = std::max(a + 1, (c + 1) * v.size() / out.size());
        float lo = INFINITY, hi = -INFINITY;
        for (std::size_t i = a; i < b && i < v.size(); ++i) {
            lo = std::min(lo, static_cast<float>(v[i] / scale));
            hi = std::max(hi, static_cast<float>(v[i] / scale));
        }
        out[c] = {lo, hi};
    }
    return out;
}

std::string db_text(double v) { return std::isfinite(v) ? format_fixed(v, 2) + " " + tr("unit.db") : (v > 0 ? "∞" : "−∞"); }

}  // namespace

ProcessingScreen::ProcessingScreen(UiContext& ui, std::shared_ptr<ProcessingModel> model) : ui_{ui}, model_{std::move(model)} { panel_boxes_.fill(kNoBox); }

std::string ProcessingScreen::tab_label() const { return tr("tui.tab.processing"); }

std::vector<Hint> ProcessingScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},       {"z", tr("tui.signal.hint.zoom")},          {"a/b/x", tr("tui.processing.hint.view")},
            {"j/J", tr("tui.decomposition.hint.levels")}, {"m", tr("tui.processing.hint.method")},  {"s", tr("tui.processing.hint.mode")},      {"[ ]", tr("tui.processing.hint.scale")},
            {tr("tui.spectrum.key.drag"), "±λ"},       {"e", tr("tui.processing.hint.eq")},        {"u", tr("tui.decomposition.hint.undo")},
            {"Space", tr("tui.processing.hint.stop")}, {"F1", tr("tui.processing.hint.f1")}};
}

std::optional<TheoryLink> ProcessingScreen::theory_link() const { return TheoryLink{TheoryLink::Kind::card, "threshold"}; }

void ProcessingScreen::publish(theory::LiveValues& v) const {
    const auto r = model_->result();
    const auto s = model_->source();
    v.source = tab_label() + " ▸ " + (s ? s->path.filename().string() : std::string{"—"});
    if (!r) return;
    const auto& o = r->options;
    v.set("block.rule", std::string{core::shrink_rule_name(o.rule)});
    v.set("block.mode", std::string{o.threshold == core::ThresholdMode::hard ? "hard" : o.threshold == core::ThresholdMode::garrote ? "garrote" : "soft"});
    v.set("block.levels", "d₁ … d" + subscript_count(o.levels));
    std::string lambdas;
    for (std::size_t b = 1; b < r->bands.size(); ++b) lambdas += (lambdas.empty() ? "" : ", ") + r->bands[b].name + " " + format_number(r->bands[b].lambda, 3);
    v.set("block.thresholds", lambdas);
    v.set("metric.sigma", format_number(r->sigma, 4));
    v.set("metric.kept", group(r->kept) + " / " + group(r->total));
    v.set("metric.energy_removed", format_fixed(r->energy_removed_pct, 1) + " %");
}

std::string ProcessingScreen::context() const {
    const auto s = model_->source();
    const auto r = model_->result();
    if (!s || !r) return {};
    const auto& o = r->options;
    const double t0 = static_cast<double>(r->start) / r->fs, t1 = t0 + static_cast<double>(r->clean.size()) / r->fs;
    return fmt::format(fmt::runtime(tr("tui.processing.context")), fmt::arg("file", s->path.filename().string()), fmt::arg("channel", o.channel == 1 ? "R" : "L"),
                       fmt::arg("t0", format_fixed(t0, 2)), fmt::arg("t1", format_fixed(t1, 2)), fmt::arg("snr", format_fixed(o.snr_db, 0)), fmt::arg("wavelet", o.wavelet),
                       fmt::arg("levels", o.levels));
}

ftxui::Element ProcessingScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1), fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element ProcessingScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), kNoBox, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

void ProcessingScreen::change(const std::function<void(ProcessingOptions&)>& edit, bool new_step) {
    if (!ui_.jobs) return;
    auto o = model_->options();
    edit(o);
    model_->set_options(o, *ui_.jobs, new_step);
}

void ProcessingScreen::set_scale(double s, bool new_step) {
    change([s](ProcessingOptions& o) { o.scale = std::clamp(s, 0.1, 10.0); }, new_step);
}

std::size_t ProcessingScreen::active_preset() const {
    const auto o = model_->options();
    if (o.gains.empty()) return 0;
    for (std::size_t p = 1; p < kPresets.size(); ++p)
        if (preset_gains(p, o.levels) == o.gains) return p;
    return kPresets.size();
}

void ProcessingScreen::set_preset(std::size_t p) {
    preset_ = p % kPresets.size();
    change([this](ProcessingOptions& o) { o.gains = preset_ == 0 ? std::vector<double>{} : preset_gains(preset_, o.levels); });
}

void ProcessingScreen::play(View v) {
    view_ = v;
    if (!ui_.player) return;
    auto clip = model_->clip(v == View::a ? ProcessingModel::Clip::noisy : v == View::b ? ProcessingModel::Clip::out : ProcessingModel::Clip::residual);
    if (!clip) return;
    if (ui_.player->state() == audio::PlayState::playing && ui_.player->tag().starts_with("processing")) {
        ui_.player->replace(std::move(*clip));
        return;
    }
    if (auto r = ui_.player->play(std::move(*clip), 0); !r) notice_ = fmt::format(fmt::runtime(tr("tui.signal.audio_unavailable")), fmt::arg("reason", r.error().message));
}

ftxui::Element ProcessingScreen::toolbar(bool two_rows, int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    int used = 0;
    auto chip = [&](std::string id, std::string label, bool on, std::function<void()> action) {
        used += display_width(label) + 2;
        auto e = text(on ? "[" + label + "]" : " " + label + " ") | fg(on ? th.accent : th.fg, ui_);
        if (on) e = e | bold;
        return clickable(std::move(id), e, std::move(action));
    };
    const auto o = model_->options();
    Elements parts{text(tr("tui.processing.threshold") + " ") | fg(th.dim, ui_)};
    for (const auto rule : {core::ShrinkRule::bayes, core::ShrinkRule::visu, core::ShrinkRule::sure, core::ShrinkRule::minimax})
        parts.push_back(chip("rule_" + std::string{core::shrink_rule_name(rule)}, std::string{core::shrink_rule_name(rule)}, o.rule == rule,
                             [this, rule] { change([rule](ProcessingOptions& p) { p.rule = rule; }); }));
    parts.push_back(text(" │") | fg(th.dim, ui_));
    for (const auto& [mode, name] : {std::pair{core::ThresholdMode::soft, "soft"}, std::pair{core::ThresholdMode::hard, "hard"}})
        parts.push_back(chip(std::string{"mode_"} + name, name, o.threshold == mode, [this, mode] { change([mode](ProcessingOptions& p) { p.threshold = mode; }); }));
    const std::string lead = (two_rows ? "" : " │ ") + std::string{"λ ×"};
    used = display_width(lead);
    Elements second{text(lead) | fg(th.dim, ui_)};
    for (const double s : kScales)
        second.push_back(chip("scale_" + format_fixed(s, 2), format_number(s, 2), std::abs(o.scale - s) < 1e-9, [this, s] { set_scale(s); }));
    const std::string view_label = " │ " + tr("tui.processing.view") + " ";
    used += display_width(view_label);
    second.push_back(text(view_label) | fg(th.dim, ui_));
    for (const auto& [v, key, id] : {std::tuple{View::a, "tui.processing.view.a", "view_a"}, std::tuple{View::b, "tui.processing.view.b", "view_b"},
                                    std::tuple{View::x, "tui.processing.view.x", "view_x"}})
        second.push_back(chip(id, tr(key), view_ == v, [this, v] { view_ = v; }));
    auto edges = " │ " + tr("tui.decomposition.edges") + " " + std::string{core::to_string(o.mode)};
    if (two_rows && used + display_width(edges) > width) edges = ellipsize(edges, std::max(0, width - used), ui_.caps.glyphs == GlyphMode::ascii);
    second.push_back(text(edges) | fg(th.dim, ui_));
    if (two_rows) return vbox({hbox(std::move(parts)), hbox(std::move(second))});
    for (auto& e : second) parts.push_back(std::move(e));
    return hbox(std::move(parts));
}

ftxui::Element ProcessingScreen::coeffs_body(const ProcessingResult& r, int width, int rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const int plot_width = std::max(10, width - kBandLabel - 4);
    Elements out;
    const auto shown = std::min<std::size_t>(r.bands.size(), static_cast<std::size_t>(std::max(1, rows / 2)));
    for (std::size_t b = 0; b < shown; ++b) {
        const auto& band = r.bands[b];
        const double scale = band.lambda > 0 ? std::min(band.max_abs, 4 * band.lambda) : std::max(band.max_abs, 1e-12);
        PlotSpec spec;
        spec.labels = false;
        spec.x_labels = false;
        spec.x = {0, 1};
        spec.y = {-1, 1};
        if (band.lambda > 0) spec.y_marks = {band.lambda / scale, -band.lambda / scale};
        spec.envelopes.push_back(Envelope{columns_of(band.coeffs, 2 * plot_width, scale), b == 0 ? th.dim : th.trace[0], {}});
        const auto& name = band.name;
        const auto info = b == 0 ? tr("tui.processing.untouched") : "λ " + format_number(band.lambda, 3) + " · " + group(band.kept) + "/" + group(band.length);
        out.push_back(hbox({vbox({hbox({text(name) | bold | fg(b == 0 ? th.dim : th.trace[0], ui_), text(" " + hz_text(band.lo_hz) + "–" + hz_text(band.hi_hz)) | fg(th.dim, ui_)}),
                                  text(info) | fg(th.dim, ui_)}) |
                                size(WIDTH, EQUAL, kBandLabel),
                            plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, 2) | size(WIDTH, EQUAL, plot_width)}));
    }
    return vbox(std::move(out));
}

ftxui::Element ProcessingScreen::signal_body(const ProcessingResult& r, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const int plot_width = std::max(10, width - 4);
    double peak = 0;
    for (const double v : r.noisy) peak = std::max(peak, std::abs(v));
    peak = std::max(peak, 1e-12);
    const auto& shown = view_ == View::a ? r.noisy : view_ == View::b ? r.out : r.residual;
    PlotSpec spec;
    spec.x = {0, static_cast<double>(r.clean.size()) / r.fs};
    spec.y = {-1, 1};
    spec.envelopes.push_back(Envelope{columns_of(r.clean, 2 * plot_width, peak), th.dim, {}});
    spec.envelopes.push_back(Envelope{columns_of(shown, 2 * plot_width, peak), view_ == View::x ? th.warn : th.trace[0], {}});
    return plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, std::max(3, height)) | size(WIDTH, EQUAL, plot_width);
}

ftxui::Element ProcessingScreen::curve_body(const ProcessingResult& r, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    PlotSpec spec;
    spec.x = {1, static_cast<double>(std::max<std::size_t>(2, r.total)), AxisScale::log10};
    spec.y = {-40, 0};
    Trace t;
    t.color = th.trace[0];
    for (const auto& p : r.curve) {
        t.x.push_back(static_cast<double>(p.k));
        t.y.push_back(std::clamp(p.err_db, -40.0, 0.0));
    }
    spec.traces.push_back(std::move(t));
    spec.x_marks = {static_cast<double>(std::max<std::size_t>(1, r.kept))};
    return plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, std::max(3, height)) | size(WIDTH, EQUAL, std::max(10, width - 4));
}

ftxui::Element ProcessingScreen::eq_body(const ProcessingResult& r, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements chips;
    for (std::size_t p = 0; p < kPresets.size(); ++p) {
        const bool on = active_preset() == p;
        chips.push_back(clickable("eq_" + std::to_string(p), text(on ? "[" + tr(kPresets[p]) + "]" : " " + tr(kPresets[p]) + " ") | fg(on ? th.accent : th.fg, ui_),
                                  [this, p] { set_preset(p); }));
    }
    PlotSpec spec;
    spec.x = {1.0 / 128, 1.0, AxisScale::log10};
    spec.y = {-40, 10};
    Trace t, a;
    t.color = th.trace[0];
    a.color = th.warn;
    a.style = LineStyle::dashed;
    for (std::size_t i = 0; i < r.transfer.omega.size(); ++i) {
        t.x.push_back(r.transfer.omega[i]);
        t.y.push_back(std::clamp(r.transfer.t_db[i], -40.0, 10.0));
        a.x.push_back(r.transfer.omega[i]);
        a.y.push_back(std::clamp(r.transfer.a_db[i], -40.0, 10.0));
    }
    spec.traces.push_back(std::move(t));
    spec.traces.push_back(std::move(a));
    return vbox({hbox(std::move(chips)),
                 plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, std::max(3, height - 1)) | size(WIDTH, EQUAL, std::max(10, width - 4))});
}

ftxui::Element ProcessingScreen::summary_body(const ProcessingResult& r) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto row = [&](const std::string& key, const std::string& value, Rgb colour) { return hbox({text(pad_right(tr(key), 13)) | fg(th.dim, ui_), text(value) | fg(colour, ui_)}); };
    const double ratio = r.kept > 0 ? static_cast<double>(r.total) / static_cast<double>(r.kept) : INFINITY;
    return vbox({row("tui.processing.snr_in", db_text(r.snr_in), th.fg), row("tui.processing.snr_out", db_text(r.snr_out), r.snr_out > r.snr_in ? th.ok : th.warn),
                 row("tui.processing.kept", group(r.kept) + " / " + group(r.total), th.fg),
                 row("tui.processing.ratio", std::isfinite(ratio) ? format_number(ratio, 3) + " : 1" : "—", th.fg),
                 row("tui.processing.mse", format_number(r.mse, 3), th.fg), row("tui.processing.energy", format_fixed(r.energy_pct, 1) + " %", th.fg),
                 text(tr("tui.processing.snr_note")) | fg(th.dim, ui_)});
}

ftxui::Element ProcessingScreen::noise_body(const ProcessingResult& r) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const double visu = r.sigma * std::sqrt(2 * std::log(static_cast<double>(std::max<std::size_t>(2, r.clean.size()))));
    return vbox({text("σ = MAD(d₁)/0,6745 = " + format_number(r.sigma, 4)) | fg(th.fg, ui_), text(tr("tui.processing.bayes_note")) | fg(th.dim, ui_),
                 text("Visu λ = σ√(2 ln N) = " + format_number(visu, 4)) | fg(th.dim, ui_),
                 text(fmt::format(fmt::runtime(tr("tui.processing.approx_untouched")), fmt::arg("a", r.bands.empty() ? std::string{"a"} : r.bands.front().name))) | fg(th.dim, ui_)});
}

ftxui::Element ProcessingScreen::listen_body() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements buttons;
    for (const auto& [v, key, id] : {std::tuple{View::a, "tui.processing.play.a", "play_a"}, std::tuple{View::b, "tui.processing.play.b", "play_b"},
                                    std::tuple{View::x, "tui.processing.play.x", "play_x"}})
        buttons.push_back(clickable(id, text("▶ " + tr(key) + " ") | fg(view_ == v ? th.accent : th.fg, ui_), [this, v] { play(v); }));
    Elements rows{hbox(std::move(buttons)), text(tr("tui.processing.seamless")) | fg(th.dim, ui_)};
    if (const auto r = model_->result(); r && r->fs > 0) {
        const double from = static_cast<double>(r->listen_start) / r->fs, span = static_cast<double>(r->audio_noisy.size()) / r->fs;
        rows.push_back(text(fmt::format(fmt::runtime(tr("tui.processing.heard")), fmt::arg("span", format_fixed(span, 2)), fmt::arg("from", format_fixed(from, 2)),
                                        fmt::arg("to", format_fixed(from + span, 2)))) |
                       fg(th.dim, ui_));
    }
    if (!notice_.empty()) rows.push_back(text(notice_) | fg(th.warn, ui_));
    return vbox(std::move(rows));
}

ftxui::Element ProcessingScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    clickables_.clear();
    if (!ui_.jobs) return text("");
    model_->sync(*ui_.jobs);
    const auto r = model_->result();
    const auto& th = ui_.theme;
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const bool small = breakpoint_ <= Breakpoint::small || maximized_;
    const bool two_rows = cols < 170;
    const int total = std::max(8, rows - 3);
    const int main_width = small ? cols : cols - kRightWidth - 1;
    if (!r) {
        const auto e = model_->error();
        const bool stopped = model_->cancelled() && !model_->job();
        Elements lines{text(e ? *e : stopped ? tr("tui.processing.cancelled") : model_->source() ? tr("tui.processing.computing") : tr("tui.processing.empty")) |
                       fg(e ? th.err : stopped ? th.warn : th.dim, ui_)};
        return vbox({toolbar(two_rows, cols), frame_panel(coeffs_panel, centered(std::move(lines)), {}, tr("tui.processing.coeffs")) | flex});
    }
    const int coeff_rows = small ? total - 4 : static_cast<int>(2 * r->bands.size());
    const int lower = small ? total / 2 : std::max(5, total - (two_rows ? 2 : 1) - 1 - (coeff_rows + 2) - 2 - 1 - 9);
    auto coeffs = frame_panel(coeffs_panel, coeffs_body(*r, main_width, coeff_rows), {{tr("tui.processing.coeffs_chip"), false}}, tr("tui.processing.coeffs"));
    const auto view_name = tr(view_ == View::a ? "tui.processing.view.a" : view_ == View::b ? "tui.processing.view.b" : "tui.processing.view.x");
    auto signal = frame_panel(signal_panel, signal_body(*r, main_width, lower), {{tr("tui.processing.signal_chip"), false}},
                              fmt::format(fmt::runtime(tr("tui.processing.signal")), fmt::arg("view", view_name)));
    const int half = small ? main_width : (main_width - 1) / 2;
    auto curve = frame_panel(curve_panel, curve_body(*r, half, 6), {{tr("tui.processing.curve_chip"), false}}, tr("tui.processing.curve"));
    auto eq = frame_panel(eq_panel, eq_body(*r, small ? main_width : main_width - half - 1, 7), {{tr("tui.processing.eq_chip"), false}}, tr("tui.processing.eq"));
    auto summary = frame_panel(summary_panel, summary_body(*r), {{std::string{core::shrink_rule_name(r->options.rule)} + " · " + format_number(r->options.scale, 2), false}},
                               tr("tui.processing.summary"));
    auto noise = frame_panel(noise_panel, noise_body(*r), {}, tr("tui.processing.noise"));
    auto listen = frame_panel(listen_panel, listen_body(), {}, tr("tui.processing.listen"));
    auto theory = frame_panel(theory_panel, paragraph(tr("tui.processing.theory_text")) | fg(th.dim, ui_), {{"F1", false}}, tr("tui.spectrum.theory"));
    if (small) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case coeffs_panel: return vbox({toolbar(two_rows, cols), coeffs | flex});
        case signal_panel: return signal;
        case curve_panel: return curve;
        case eq_panel: return eq;
        case summary_panel: return summary;
        case noise_panel: return noise;
        case listen_panel: return listen;
        case theory_panel: return theory;
        case panel_count: break;
        }
    }
    auto left = vbox({coeffs, signal | flex, hbox({curve | size(WIDTH, EQUAL, half), text(" "), eq | flex})});
    auto right = vbox({summary, noise, listen, theory | flex});
    return vbox({toolbar(two_rows, cols), text(""), hbox({left | flex, text(" "), right | size(WIDTH, EQUAL, kRightWidth)}) | flex});
}

bool ProcessingScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    using ftxui::Mouse;
    if (!ui_.jobs) return false;
    auto& jobs = *ui_.jobs;
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
            for (auto& c : clickables_)
                if (c.box.Contain(m.x, m.y)) {
                    auto action = c.action;
                    action();
                    return true;
                }
            for (int p = 0; p < panel_count; ++p)
                if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) focus_ = static_cast<Panel>(p);
            if (panel_boxes_[coeffs_panel].Contain(m.x, m.y)) drag_ = {true, false, m.y, m.y, model_->options().scale};
            return true;
        }
        if (drag_.active) {
            if (m.y != drag_.last_y) {
                const double before = model_->options().scale;
                set_scale(drag_.scale * std::pow(1.15, drag_.y - m.y), !drag_.moved);
                if (model_->options().scale != before) drag_.moved = true;
                drag_.last_y = m.y;
            }
            if (m.motion == Mouse::Released) drag_.active = false;
            return true;
        }
        return false;
    }
    if (e == Event::Escape) {
        if (ui_.player && ui_.player->state() == audio::PlayState::playing) ui_.player->stop();
        else if (model_->job()) model_->cancel(jobs);
        else if (maximized_) maximized_.reset();
        else return false;
        return true;
    }
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
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
    if (e == Event::Character('a')) return play(View::a), true;
    if (e == Event::Character('b')) return play(View::b), true;
    if (e == Event::Character('x')) return play(View::x), true;
    if (e == Event::Character(' ')) {
        if (ui_.player) ui_.player->stop();
        return true;
    }
    if (e == Event::Character('m')) {
        change([](ProcessingOptions& o) {
            o.rule = o.rule == core::ShrinkRule::bayes  ? core::ShrinkRule::visu
                     : o.rule == core::ShrinkRule::visu ? core::ShrinkRule::sure
                     : o.rule == core::ShrinkRule::sure ? core::ShrinkRule::minimax
                                                        : core::ShrinkRule::bayes;
        });
        return true;
    }
    if (e == Event::Character('s')) {
        change([](ProcessingOptions& o) { o.threshold = o.threshold == core::ThresholdMode::soft ? core::ThresholdMode::hard : core::ThresholdMode::soft; });
        return true;
    }
    if (e == Event::Character('[') || e == Event::Character(']')) {
        const double now = model_->options().scale;
        double next = now;
        if (e == Event::Character(']')) {
            for (const double s : kScales)
                if (s > now + 1e-9) {
                    next = s;
                    break;
                }
        } else {
            for (auto it = kScales.rbegin(); it != kScales.rend(); ++it)
                if (*it < now - 1e-9) {
                    next = *it;
                    break;
                }
        }
        set_scale(next);
        return true;
    }
    if (e == Event::Character('e')) return set_preset((active_preset() < kPresets.size() ? active_preset() : preset_) + 1), true;
    if (e == Event::Character('j')) return change([](ProcessingOptions& o) { o.levels = std::min<std::size_t>(10, o.levels + 1); }), true;
    if (e == Event::Character('J')) return change([](ProcessingOptions& o) { o.levels = std::max<std::size_t>(1, o.levels - 1); }), true;
    if (e == Event::Character('r')) return model_->recompute(jobs), true;
    if (e == Event::Character('u')) return model_->undo(jobs), true;
    return false;
}

ftxui::Box ProcessingScreen::area(std::string_view id) const {
    if (id == "coeffs_panel") return panel_boxes_[coeffs_panel];
    if (id == "summary_panel") return panel_boxes_[summary_panel];
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

void ProcessingScreen::open_with(const NavArgs& args) {
    change([&args](ProcessingOptions& o) {
        if (!args.wavelet.empty()) o.wavelet = args.wavelet;
        if (args.n) o.levels = std::clamp<std::size_t>(*args.n, 1, 10);
    });
}

std::optional<std::pair<int, int>> ProcessingScreen::menu_anchor() const {
    const auto& b = panel_boxes_[static_cast<std::size_t>(focus_)];
    if (b.x_max < b.x_min) return std::nullopt;
    return std::pair{(b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2};
}

std::shared_ptr<TabScreen> make_processing_screen(UiContext& ui, std::shared_ptr<ProcessingModel> model) {
    return std::make_shared<ProcessingScreen>(ui, std::move(model));
}

}  // namespace signals::tui
