#include "tui/screens/resample_screen.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "tui/shell/errors.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"

namespace signals::tui {
namespace {

constexpr double kSyntheticRate = 48000;

std::string hz(double f) { return format_number(f, 6) + " " + tr("unit.hz"); }

}  // namespace

ResampleScreen::ResampleScreen(UiContext& ui, std::shared_ptr<SignalModel> signal) : ui_{ui}, signal_{std::move(signal)} {}

std::string ResampleScreen::tab_label() const { return tr("tui.sub.resample"); }

std::vector<Hint> ResampleScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},     {"z", tr("tui.signal.hint.zoom")},
            {"m", tr("tui.resample.hint.mode")},     {"+ −", "M"},                              {"> <", "L"},
            {"a", tr("tui.resample.hint.filter")},   {"c/C", tr("tui.resample.hint.cutoff")},   {"s", tr("tui.resample.hint.source")},
            {"f/F", tr("tui.resample.hint.tone")},   {"F1", tr("tui.resample.hint.f1")}};
}

std::optional<TheoryLink> ResampleScreen::theory_link() const {
    const bool up = options_.mode == ResampleMode::up || (options_.mode == ResampleMode::rational && options_.l > options_.m);
    return TheoryLink{TheoryLink::Kind::card, up ? "upsample" : "downsample"};
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

ftxui::Element ResampleScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), ftxui::Box{0, -1, 0, -1}, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element ResampleScreen::chip(std::string id, const std::string& label, bool on, std::function<void()> action) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto t = text(label);
    t = on ? t | bold | fg(th.bg, ui_) | bg(th.trace[0], ui_) : t | fg(th.fg, ui_);
    return hbox({clickable(std::move(id), t, std::move(action)), text(" ")});
}

ftxui::Element ResampleScreen::frame(Panel p, std::string title, std::vector<Chip> chips, ftxui::Element body) {
    static constexpr const char* kNames[]{"controls", "before", "after", "readouts", "time"};
    const std::string name = kNames[static_cast<std::size_t>(p)];
    for (auto& c : chips) c.box = notes_.box("legend." + name);
    if (breakpoint_ <= Breakpoint::small && !maximized_ && p != controls_panel) {
        const auto it = std::find(small_panels_.begin(), small_panels_.end(), p);
        if (it != small_panels_.end())
            chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(it - small_panels_.begin()) + 1),
                                         fmt::arg("count", static_cast<int>(small_panels_.size()))),
                             false});
    }
    return panel(std::move(title), std::move(chips), std::move(body), focus_ == p, ui_, notes_.box("panel." + name)) |
           ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element ResampleScreen::controls() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& o = options_;
    auto set = [this](auto f) { return [this, f] { f(options_); }; };
    Elements first{notes_.mark("mode", text(tr("tui.resample.mode") + " ") | fg(th.dim, ui_))};
    first.push_back(chip("mode_down", "↓M", o.mode == ResampleMode::down, set([](ResampleOptions& p) { p.mode = ResampleMode::down; })));
    first.push_back(chip("mode_up", "↑L", o.mode == ResampleMode::up, set([](ResampleOptions& p) { p.mode = ResampleMode::up; })));
    first.push_back(chip("mode_rational", "L/M", o.mode == ResampleMode::rational, set([](ResampleOptions& p) { p.mode = ResampleMode::rational; })));
    first.push_back(notes_.mark("m", text(" M = " + std::to_string(o.m) + " ") | fg(th.fg, ui_)));
    first.push_back(chip("m_less", "−", false, set([](ResampleOptions& p) { p.m = p.m > 1 ? p.m - 1 : 1; })));
    first.push_back(chip("m_more", "+", false, set([](ResampleOptions& p) { p.m = std::min(kMaxFactor, p.m + 1); })));
    first.push_back(notes_.mark("l", text(" L = " + std::to_string(o.l) + " ") | fg(th.fg, ui_)));
    first.push_back(chip("l_less", "<", false, set([](ResampleOptions& p) { p.l = p.l > 1 ? p.l - 1 : 1; })));
    first.push_back(chip("l_more", ">", false, set([](ResampleOptions& p) { p.l = std::min(kMaxFactor, p.l + 1); })));
    Elements second{notes_.mark("filter", text(tr("tui.resample.filter") + " ") | fg(th.dim, ui_))};
    second.push_back(chip("filter_on", tr("tui.resample.filter.on"), o.filter == AntiAlias::on, set([](ResampleOptions& p) { p.filter = AntiAlias::on; })));
    second.push_back(chip("filter_bypass", tr("tui.resample.filter.bypass"), o.filter == AntiAlias::bypass, set([](ResampleOptions& p) { p.filter = AntiAlias::bypass; })));
    second.push_back(notes_.mark("cutoff", text(" " + tr("tui.resample.cutoff") + " ×" + format_fixed(o.cutoff, 2) + "  ") | fg(th.fg, ui_)));
    second.push_back(notes_.mark("source", text(tr("tui.resample.source") + " ") | fg(th.dim, ui_)));
    second.push_back(chip("source_tone", tr("tui.resample.source.tone") + " " + hz(o.tone_hz), o.source == ResampleSource::tone, set([](ResampleOptions& p) { p.source = ResampleSource::tone; })));
    second.push_back(chip("source_sweep", tr("tui.resample.source.sweep"), o.source == ResampleSource::sweep, set([](ResampleOptions& p) { p.source = ResampleSource::sweep; })));
    second.push_back(chip("source_file", tr("tui.resample.source.file"), o.source == ResampleSource::file, set([](ResampleOptions& p) { p.source = ResampleSource::file; })));
    Elements body{hbox(std::move(first)) | xflex, hbox(std::move(second)) | xflex};
    if (!notice_.empty()) body.push_back(paragraph(notice_) | fg(th.warn, ui_));
    std::string title = tr("tui.resample.title");
    if (view_) title += fmt::format(" · {} → {}", hz(view_->fs), hz(view_->fs_out));
    return frame(controls_panel, title, {}, vbox(std::move(body)));
}

ftxui::Element ResampleScreen::before_body() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return text("—") | fg(th.dim, ui_);
    const auto& v = *view_;
    PlotSpec spec;
    spec.x = zoom_.axis("before", {0, v.fs / 2}).axis();
    spec.y = {-140, 5};
    Trace in{v.psd_in_db, v.f_in, th.trace[0]};
    spec.traces.push_back(std::move(in));
    if (filter_panel(v) == FilterPanel::before && !v.filter_f.empty()) {
        Trace h{v.filter_db, v.filter_f, th.trace[3], LineStyle::dashed};
        spec.traces.push_back(std::move(h));
    }
    if (v.fs_out < v.fs) {
        for (double k = 1; k * v.fs_out / 2 < v.fs / 2; ++k) spec.x_marks.push_back(k * v.fs_out / 2);
        spec.selection = std::pair{v.fs_out / 2, v.fs / 2};
    }
    if (options_.source == ResampleSource::tone) spec.cursor_a = options_.tone_hz;
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
    return zoom_.track("before", std::move(spec), std::move(drawn), {tr("tui.resample.before"), {}, tr("tui.export.freq_hz"), tr("tui.filters.db")});
}

ftxui::Element ResampleScreen::after_body() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return text("—") | fg(th.dim, ui_);
    const auto& v = *view_;
    PlotSpec spec;
    spec.x = zoom_.axis("after", {0, v.fs_out / 2}).axis();
    spec.y = {-140, 5};
    for (std::size_t k = 0; k < v.copies_db.size(); ++k) {
        Trace c{v.copies_db[k], v.f_out, th.trace[(1 + k) % th.trace.size()], LineStyle::dotted};
        spec.traces.push_back(std::move(c));
    }
    if (filter_panel(v) == FilterPanel::after && !v.filter_f.empty()) {
        Trace h{v.filter_db, v.filter_f, th.trace[3], LineStyle::dashed};
        spec.traces.push_back(std::move(h));
    }
    Trace out{v.psd_out_db, v.f_out, th.trace[0]};
    spec.traces.push_back(std::move(out));
    if (v.up > 1)
        for (double k = 1; k * v.fs < v.fs_out / 2 + 1; ++k) spec.x_marks.push_back(k * v.fs);
    if (v.tone_out) spec.cursor_a = *v.tone_out;
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
    return zoom_.track("after", std::move(spec), std::move(drawn), {tr("tui.resample.after"), {}, tr("tui.export.freq_hz"), tr("tui.filters.db")});
}

ftxui::Element ResampleScreen::readouts_body() {
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
        rows.push_back(text(tr(v.up > 1 && v.down == 1 ? "tui.resample.filter_off_line_up" : "tui.resample.filter_off_line")) | fg(th.warn, ui_));
    rows.push_back(text(v.up > 1 && v.down == 1 ? "Y(ω) = X(Lω) — L − 1 " + tr("tui.resample.images") : "Y(ω) = (1/M) Σₖ X((ω − 2πk)/M)") | fg(th.dim, ui_));
    return vbox(std::move(rows));
}

ftxui::Element ResampleScreen::time_body() {
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
    spec.x = zoom_.axis("time", {-0.5, static_cast<double>(std::max<std::size_t>(1, v.y_head.size() * v.down)) - 0.5}).axis();
    spec.y = {lo - 0.1 * (hi - lo + 1e-9), hi + 0.1 * (hi - lo + 1e-9)};
    spec.traces.push_back(std::move(in));
    spec.traces.push_back(std::move(out));
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
    return zoom_.track("time", std::move(spec), std::move(drawn), {tr("tui.resample.time"), {}, tr("tui.export.time_s"), ""});
}

std::vector<ResampleScreen::Panel> ResampleScreen::shown_panels(int cols) const {
    std::vector<Panel> out{before_panel, after_panel, readouts_panel};
    if (view_ && view_->up > 1 && (cols >= 120 || breakpoint_ <= Breakpoint::small)) out.push_back(time_panel);
    return out;
}

ftxui::Element ResampleScreen::render_panel(Panel p) {
    std::vector<Chip> chips;
    switch (p) {
    case controls_panel: return controls();
    case before_panel: return frame(p, tr("tui.resample.before") + (view_ ? " · fs = " + hz(view_->fs) : std::string{}), {}, before_body());
    case after_panel:
        if (view_ && !view_->copies_db.empty())
            chips.push_back({fmt::format(fmt::runtime(tr("tui.resample.copies")), fmt::arg("count", tr_count("unit.copy", static_cast<long long>(view_->copies_db.size())))), false});
        return frame(p, tr("tui.resample.after") + (view_ ? " · fs′ = " + hz(view_->fs_out) : std::string{}), std::move(chips), after_body());
    case readouts_panel: return frame(p, tr("tui.resample.readouts"), {}, readouts_body());
    case time_panel: return frame(p, tr("tui.resample.time"), {}, time_body());
    case panel_count: break;
    }
    return ftxui::text("");
}

ftxui::Element ResampleScreen::OnRender() {
    using namespace ftxui;
    clickables_.clear();
    notes_.clear();
    panel_boxes_.fill(kNoBox);
    zoom_.begin_frame();
    refresh();
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const auto shown = shown_panels(cols);
    if (maximized_) {
        if (*maximized_ == time_panel && std::find(shown.begin(), shown.end(), time_panel) == shown.end()) maximized_ = before_panel;
        focus_ = *maximized_;
        return render_panel(*maximized_) | flex;
    }
    if (breakpoint_ <= Breakpoint::small) {
        small_panels_ = shown;
        const auto p = shown[layout_.small_index() % shown.size()];
        if (focus_ != controls_panel) focus_ = p;
        return vbox({controls(), render_panel(p) | flex});
    }
    small_panels_.clear();
    auto plots = cols >= 120 ? hbox({render_panel(before_panel) | flex, text(" "), render_panel(after_panel) | flex})
                             : vbox({render_panel(before_panel) | flex, render_panel(after_panel) | flex});
    const bool time = std::find(shown.begin(), shown.end(), time_panel) != shown.end();
    auto bottom = time ? hbox({render_panel(readouts_panel) | flex, text(" "), render_panel(time_panel) | size(WIDTH, EQUAL, cols / 3)}) : render_panel(readouts_panel);
    return vbox({controls(), std::move(plots) | flex, std::move(bottom)});
}

std::vector<MenuItem> ResampleScreen::context_menu(int x, int y) {
    if (auto items = zoom_.menu(x, y, ui_); !items.empty()) return items;
    return {};
}

std::vector<JumpTarget> ResampleScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
}

std::optional<std::string> ResampleScreen::explain_at(int x, int y) const {
    for (const auto& c : clickables_)
        if (valid(c.box) && c.box.Contain(x, y)) return notes_.explain(c.id);
    return notes_.at(x, y);
}

ftxui::Box ResampleScreen::area(std::string_view id) const {
    if (id == "before_plot") return zoom_.box("before");
    if (id == "after_plot") return zoom_.box("after");
    if (id == "time_plot") return zoom_.box("time");
    if (id == "controls_panel") return panel_boxes_[controls_panel];
    if (id == "before_panel") return panel_boxes_[before_panel];
    if (id == "after_panel") return panel_boxes_[after_panel];
    if (id == "readouts_panel") return panel_boxes_[readouts_panel];
    if (id == "time_panel") return panel_boxes_[time_panel];
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

bool ResampleScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        if (zoom_.on_mouse(m)) return true;
        if (m.button != ftxui::Mouse::Left || m.motion != ftxui::Mouse::Pressed) return false;
        for (auto& c : clickables_)
            if (c.box.Contain(m.x, m.y)) {
                c.action();
                return true;
            }
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) {
                focus_ = static_cast<Panel>(p);
                zoom_.press(m.x, m.y);
                return true;
            }
        return false;
    }
    if (e == Event::Escape) {
        if (!maximized_) return false;
        maximized_.reset();
        return true;
    }
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse) {
        const bool back = e == Event::TabReverse;
        const std::size_t step = back ? panel_count - 1 : 1;
        if (breakpoint_ <= Breakpoint::small && !maximized_) {
            layout_.next_panel(std::max<std::size_t>(1, small_panels_.size()), back);
            focus_ = before_panel;
            return true;
        }
        auto next = static_cast<Panel>((focus_ + step) % panel_count);
        if (next == time_panel && !(view_ && view_->up > 1)) next = static_cast<Panel>((next + step) % panel_count);
        focus_ = next;
        if (maximized_) maximized_ = focus_;
        return true;
    }
    if (e == Event::Character('0')) {
        const char* id = focus_ == before_panel ? "before" : focus_ == after_panel ? "after" : focus_ == time_panel ? "time" : nullptr;
        if (id) return zoom_.on_key(e, id);
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
