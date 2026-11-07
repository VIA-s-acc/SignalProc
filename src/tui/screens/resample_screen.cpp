#include "tui/screens/resample_screen.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "tui/shell/errors.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"

namespace signals::tui {
namespace {

constexpr double kSyntheticRate = 48000;

std::string hz(double f) { return format_number(f, 6) + " " + tr("unit.hz"); }

}  // namespace

ResampleScreen::ResampleScreen(UiContext& ui, std::shared_ptr<SignalModel> signal) : ui_{ui}, signal_{std::move(signal)} {}

std::string ResampleScreen::tab_label() const { return tr("tui.sub.resample"); }

std::vector<Hint> ResampleScreen::hints() const {
    return {{"m", tr("tui.resample.hint.mode")},     {"+ −", "M"},                              {"> <", "L"},
            {"a", tr("tui.resample.hint.filter")},   {"c/C", tr("tui.resample.hint.cutoff")},   {"s", tr("tui.resample.hint.source")},
            {"f/F", tr("tui.resample.hint.tone")},   {"F1", tr("tui.resample.hint.f1")}};
}

std::optional<TheoryLink> ResampleScreen::theory_link() const {
    return TheoryLink{TheoryLink::Kind::card, options_.mode == ResampleMode::up ? "upsample" : "downsample"};
}

void ResampleScreen::publish(theory::LiveValues& v) const {
    v.source = tab_label();
    v.set("block.m", std::to_string(options_.mode == ResampleMode::up ? options_.l : options_.m));
    if (!view_) return;
    v.set("block.taps", std::to_string(view_->taps));
    v.set("block.anti_alias", tr(options_.filter == AntiAlias::on ? "tui.resample.filter.on" : "tui.resample.filter.bypass"));
    v.set("block.attenuation_db", format_fixed(view_->aa_attenuation_db, 1) + " " + tr("tui.unit.db"));
}

void ResampleScreen::refresh() {
    const auto data = signal_ ? signal_->data() : nullptr;
    const bool from_file = options_.source == ResampleSource::file;
    const auto source_data = from_file ? data : nullptr;
    const bool usable = source_data && !source_data->samples.empty() && !source_data->samples.front().empty();
    start_ = 0;
    if (usable) {
        const auto& s = source_data->samples.front();
        if (const auto sel = signal_->selection()) {
            start_ = std::min(s.size() - 1, static_cast<std::size_t>(std::max(0.0, sel->first) * source_data->info.sample_rate));
        } else {
            if (source_data != peak_data_) {
                const auto peak = static_cast<std::size_t>(std::max_element(s.begin(), s.end(), [](float l, float r) { return std::abs(l) < std::abs(r); }) - s.begin());
                peak_start_ = peak > kResampleLength / 2 ? std::min(peak - kResampleLength / 2, s.size() > kResampleLength ? s.size() - kResampleLength : std::size_t{0}) : 0;
                peak_data_ = source_data;
            }
            start_ = peak_start_;
        }
    }
    if (computed_ && *computed_ == options_ && computed_data_ == source_data && computed_start_ == start_ && (view_ || !notice_.empty())) return;
    computed_ = options_;
    computed_data_ = source_data;
    computed_start_ = start_;
    notice_.clear();
    std::vector<double> x;
    double fs = kSyntheticRate;
    if (from_file) {
        if (!usable) {
            notice_ = tr("tui.resample.no_file");
            view_.reset();
            return;
        }
        const auto& s = source_data->samples.front();
        fs = source_data->info.sample_rate;
        x.assign(s.begin() + static_cast<std::ptrdiff_t>(start_), s.begin() + static_cast<std::ptrdiff_t>(std::min(s.size(), start_ + kResampleLength)));
    } else {
        x = resample_source(options_, fs, kResampleLength);
    }
    auto v = compute_resample(options_, x, fs);
    if (!v) {
        notice_ = error_text(v.error());
        view_.reset();
        return;
    }
    view_ = std::make_shared<const ResampleView>(std::move(*v));
}

ftxui::Element ResampleScreen::clickable(ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({ftxui::Box{0, -1, 0, -1}, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element ResampleScreen::chip(const std::string& label, bool on, std::function<void()> action) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto t = text(label);
    t = on ? t | bold | fg(th.bg, ui_) | bg(th.trace[0], ui_) : t | fg(th.fg, ui_);
    return hbox({clickable(t, std::move(action)), text(" ")});
}

ftxui::Element ResampleScreen::controls(int) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& o = options_;
    auto set = [this](auto f) { return [this, f] { f(options_); }; };
    Elements first{text(tr("tui.resample.mode") + " ") | fg(th.dim, ui_)};
    first.push_back(chip("↓M", o.mode == ResampleMode::down, set([](ResampleOptions& p) { p.mode = ResampleMode::down; })));
    first.push_back(chip("↑L", o.mode == ResampleMode::up, set([](ResampleOptions& p) { p.mode = ResampleMode::up; })));
    first.push_back(chip("L/M", o.mode == ResampleMode::rational, set([](ResampleOptions& p) { p.mode = ResampleMode::rational; })));
    first.push_back(text(" M = " + std::to_string(o.m) + " ") | fg(th.fg, ui_));
    first.push_back(chip("−", false, set([](ResampleOptions& p) { p.m = p.m > 1 ? p.m - 1 : 1; })));
    first.push_back(chip("+", false, set([](ResampleOptions& p) { p.m = std::min(kMaxFactor, p.m + 1); })));
    first.push_back(text(" L = " + std::to_string(o.l) + " ") | fg(th.fg, ui_));
    first.push_back(chip("<", false, set([](ResampleOptions& p) { p.l = p.l > 1 ? p.l - 1 : 1; })));
    first.push_back(chip(">", false, set([](ResampleOptions& p) { p.l = std::min(kMaxFactor, p.l + 1); })));
    Elements second{text(tr("tui.resample.filter") + " ") | fg(th.dim, ui_)};
    second.push_back(chip(tr("tui.resample.filter.on"), o.filter == AntiAlias::on, set([](ResampleOptions& p) { p.filter = AntiAlias::on; })));
    second.push_back(chip(tr("tui.resample.filter.bypass"), o.filter == AntiAlias::bypass, set([](ResampleOptions& p) { p.filter = AntiAlias::bypass; })));
    second.push_back(text(" " + tr("tui.resample.cutoff") + " ×" + format_fixed(o.cutoff, 2) + "  ") | fg(th.fg, ui_));
    second.push_back(text(tr("tui.resample.source") + " ") | fg(th.dim, ui_));
    second.push_back(chip(tr("tui.resample.source.tone") + " " + hz(o.tone_hz), o.source == ResampleSource::tone, set([](ResampleOptions& p) { p.source = ResampleSource::tone; })));
    second.push_back(chip(tr("tui.resample.source.sweep"), o.source == ResampleSource::sweep, set([](ResampleOptions& p) { p.source = ResampleSource::sweep; })));
    second.push_back(chip(tr("tui.resample.source.file"), o.source == ResampleSource::file, set([](ResampleOptions& p) { p.source = ResampleSource::file; })));
    Elements body{hbox(std::move(first)), hbox(std::move(second))};
    if (!notice_.empty()) body.push_back(text(notice_) | fg(th.warn, ui_));
    std::string title = tr("tui.resample.title");
    if (view_) title += fmt::format(" · {} → {}", hz(view_->fs), hz(view_->fs_out));
    return panel(title, {}, vbox(std::move(body)), true, ui_);
}

ftxui::Element ResampleScreen::before_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return panel(tr("tui.resample.before"), {}, text("—") | fg(th.dim, ui_), false, ui_);
    const auto& v = *view_;
    PlotSpec spec;
    spec.x = {0, v.fs / 2};
    spec.y = {-140, 5};
    Trace in{v.psd_in_db, v.f_in, th.trace[0]};
    spec.traces.push_back(std::move(in));
    if (v.down > 1 && v.up == 1 && !v.filter_f.empty()) {
        Trace h{v.filter_db, v.filter_f, th.trace[3], LineStyle::dashed};
        spec.traces.push_back(std::move(h));
    }
    if (v.fs_out < v.fs) {
        for (double k = 1; k * v.fs_out / 2 < v.fs / 2; ++k) spec.x_marks.push_back(k * v.fs_out / 2);
        spec.selection = std::pair{v.fs_out / 2, v.fs / 2};
    }
    if (options_.source == ResampleSource::tone) spec.cursor_a = options_.tone_hz;
    return panel(tr("tui.resample.before") + " · fs = " + hz(v.fs), {}, plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | flex, false, ui_);
}

ftxui::Element ResampleScreen::after_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return panel(tr("tui.resample.after"), {}, text("—") | fg(th.dim, ui_), false, ui_);
    const auto& v = *view_;
    PlotSpec spec;
    spec.x = {0, v.fs_out / 2};
    spec.y = {-140, 5};
    for (std::size_t k = 0; k < v.copies_db.size(); ++k) {
        Trace c{v.copies_db[k], v.f_out, th.trace[(1 + k) % th.trace.size()], LineStyle::dotted};
        spec.traces.push_back(std::move(c));
    }
    if (v.up > 1 && !v.filter_f.empty()) {
        Trace h{v.filter_db, v.filter_f, th.trace[3], LineStyle::dashed};
        spec.traces.push_back(std::move(h));
    }
    Trace out{v.psd_out_db, v.f_out, th.trace[0]};
    spec.traces.push_back(std::move(out));
    if (v.up > 1)
        for (double k = 1; k * v.fs < v.fs_out / 2 + 1; ++k) spec.x_marks.push_back(k * v.fs);
    if (v.tone_out) spec.cursor_a = *v.tone_out;
    std::vector<Chip> chips;
    if (!v.copies_db.empty()) chips.push_back({fmt::format(fmt::runtime(tr("tui.resample.copies")), fmt::arg("count", tr_count("unit.copy", static_cast<long long>(v.copies_db.size())))), false});
    return panel(tr("tui.resample.after") + " · fs′ = " + hz(v.fs_out), chips, plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | flex, false, ui_);
}

ftxui::Element ResampleScreen::readouts() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return text("");
    const auto& v = *view_;
    Elements rows;
    rows.push_back(text(fmt::format(fmt::runtime(tr("tui.resample.rates")), fmt::arg("fs", hz(v.fs)), fmt::arg("out", hz(v.fs_out)), fmt::arg("up", v.up),
                                    fmt::arg("down", v.down))) |
                   fg(th.fg, ui_));
    if (v.tone_out) {
        const double f = options_.tone_hz, half = v.fs_out / 2;
        const double att = -v.tone_gain_db;
        std::string key = "tui.resample.tone_kept";
        Rgb colour = th.fg;
        if (v.down > 1 && std::abs(f - half) < 0.5) key = "tui.resample.tone_edge", colour = th.warn;
        else if (f <= half && att >= 3) key = "tui.resample.tone_kept_attenuated", colour = th.warn;
        else if (f > half && att >= 60) key = "tui.resample.tone_removed", colour = th.ok;
        else if (f > half && att >= 3) key = "tui.resample.tone_folded_weak", colour = th.warn;
        else if (f > half) key = "tui.resample.tone_folded", colour = th.warn;
        rows.push_back(text(fmt::format(fmt::runtime(tr(key)), fmt::arg("f", hz(f)), fmt::arg("out", hz(*v.tone_out)), fmt::arg("db", format_fixed(att, 0)))) |
                       fg(colour, ui_));
        if (v.up > 1)
            rows.push_back(text(fmt::format(fmt::runtime(tr(options_.filter == AntiAlias::on ? "tui.resample.images_suppressed" : "tui.resample.images_kept")),
                                            fmt::arg("img", hz(v.fs - f)), fmt::arg("db", format_fixed(-v.image_gain_db, 0)))) |
                           fg(th.fg, ui_));
    }
    if (options_.filter == AntiAlias::on && v.taps > 1)
        rows.push_back(text(fmt::format(fmt::runtime(tr("tui.resample.filter_line")), fmt::arg("taps", tr_count("unit.tap", static_cast<long long>(v.taps))), fmt::arg("db", format_fixed(v.aa_attenuation_db, 1)))) | fg(th.fg, ui_));
    else
        rows.push_back(text(tr("tui.resample.filter_off_line")) | fg(th.warn, ui_));
    rows.push_back(text(v.up > 1 && v.down == 1 ? "Y(ω) = X(Lω) — L − 1 " + tr("tui.resample.images") : "Y(ω) = (1/M) Σₖ X((ω − 2πk)/M)") | fg(th.dim, ui_));
    return panel(tr("tui.resample.readouts"), {}, vbox(std::move(rows)), false, ui_);
}

ftxui::Element ResampleScreen::time_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return text("");
    const auto& v = *view_;
    Trace in{{}, {}, th.dim};
    in.stems = true;
    for (std::size_t k = 0; k < v.x_head.size() && k * v.up < v.y_head.size() * v.down; ++k) {
        in.x.push_back(static_cast<double>(k * v.up));
        in.y.push_back(v.x_head[k]);
    }
    Trace out{{}, {}, th.trace[0]};
    out.stems = true;
    for (std::size_t k = 0; k < v.y_head.size(); ++k) {
        out.x.push_back(static_cast<double>(k * v.down));
        out.y.push_back(v.y_head[k]);
    }
    double lo = 0, hi = 0;
    for (double y : out.y) lo = std::min(lo, y), hi = std::max(hi, y);
    for (double y : in.y) lo = std::min(lo, y), hi = std::max(hi, y);
    PlotSpec spec;
    spec.x = {-0.5, static_cast<double>(std::max<std::size_t>(1, v.y_head.size() * v.down)) - 0.5};
    spec.y = {lo - 0.1 * (hi - lo + 1e-9), hi + 0.1 * (hi - lo + 1e-9)};
    spec.traces.push_back(std::move(in));
    spec.traces.push_back(std::move(out));
    return panel(tr("tui.resample.time"), {}, plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | flex, false, ui_);
}

ftxui::Element ResampleScreen::OnRender() {
    using namespace ftxui;
    clickables_.clear();
    refresh();
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    auto plots = cols >= 120 ? hbox({before_panel() | flex, text(" "), after_panel() | flex}) : vbox({before_panel() | flex, after_panel() | flex});
    const bool time = view_ && view_->up > 1 && cols >= 120;
    auto bottom = time ? hbox({readouts() | flex, text(" "), time_panel() | size(WIDTH, EQUAL, cols / 3)}) : readouts();
    return vbox({controls(cols), std::move(plots) | flex, std::move(bottom)});
}

bool ResampleScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        if (m.button != ftxui::Mouse::Left || m.motion != ftxui::Mouse::Pressed) return false;
        for (auto& c : clickables_)
            if (c.box.Contain(m.x, m.y)) {
                c.action();
                return true;
            }
        return false;
    }
    auto& o = options_;
    if (e == Event::Character('m')) {
        o.mode = o.mode == ResampleMode::down ? ResampleMode::up : o.mode == ResampleMode::up ? ResampleMode::rational : ResampleMode::down;
        return true;
    }
    if (e == Event::Character('+') || e == Event::Character('=')) return o.m = std::min(kMaxFactor, o.m + 1), true;
    if (e == Event::Character('-')) return o.m = o.m > 1 ? o.m - 1 : 1, true;
    if (e == Event::Character('>') || e == Event::Character('.')) return o.l = std::min(kMaxFactor, o.l + 1), true;
    if (e == Event::Character('<') || e == Event::Character(',')) return o.l = o.l > 1 ? o.l - 1 : 1, true;
    if (e == Event::Character('a')) return o.filter = o.filter == AntiAlias::on ? AntiAlias::bypass : AntiAlias::on, true;
    if (e == Event::Character('c')) return o.cutoff = std::max(0.5, o.cutoff - 0.05), true;
    if (e == Event::Character('C')) return o.cutoff = std::min(1.0, o.cutoff + 0.05), true;
    if (e == Event::Character('s')) {
        o.source = o.source == ResampleSource::tone ? ResampleSource::sweep : o.source == ResampleSource::sweep ? ResampleSource::file : ResampleSource::tone;
        return true;
    }
    if (e == Event::Character('f')) return o.tone_hz = std::min(kSyntheticRate / 2 - 100, o.tone_hz + 250), true;
    if (e == Event::Character('F')) return o.tone_hz = std::max(100.0, o.tone_hz - 250), true;
    return false;
}

std::shared_ptr<TabScreen> make_resample_screen(UiContext& ui, std::shared_ptr<SignalModel> signal) {
    return std::make_shared<ResampleScreen>(ui, std::move(signal));
}

}  // namespace signals::tui
