#include "tui/screens/tempo_screen.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "tui/shell/errors.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

std::string hz(double f) { return format_number(f, 4) + " " + tr("unit.hz"); }

std::string seconds(double s) { return format_number(s, 3) + " " + tr("unit.s"); }

std::string semitones_label(double s) { return (s > 0 ? "+" : s < 0 ? "−" : "") + format_number(std::abs(s), 3) + " " + tr("tui.tempo.semitones"); }

std::string_view mode_id(TempoMode m) { return m == TempoMode::stretch ? "stretch" : m == TempoMode::shift ? "shift" : "speed"; }

std::string_view source_id(TempoSource s) { return s == TempoSource::tone ? "tone" : s == TempoSource::vibrato ? "vibrato" : "file"; }

Trace pitch_trace(const std::vector<double>& f0, double frame_seconds, double hop_seconds, Rgb color) {
    Trace t{{}, {}, color};
    for (std::size_t k = 0; k < f0.size(); ++k)
        if (f0[k] > 0) {
            t.x.push_back(frame_seconds / 2 + static_cast<double>(k) * hop_seconds);
            t.y.push_back(f0[k]);
        }
    return t;
}

Envelope wave(std::shared_ptr<const TempoView> v, bool processed, Axis view, Rgb color) {
    return Envelope{{}, color, [v = std::move(v), processed, view](int cols) {
                        const auto& s = processed ? v->y : v->x;
                        const double fs = v->fs;
                        std::vector<MinMaxPyramid::Range> out(static_cast<std::size_t>(std::max(1, cols)), MinMaxPyramid::Range{0, 0});
                        for (std::size_t c = 0; c < out.size(); ++c) {
                            const double t0 = view.lo + (view.hi - view.lo) * static_cast<double>(c) / static_cast<double>(out.size());
                            const double t1 = view.lo + (view.hi - view.lo) * static_cast<double>(c + 1) / static_cast<double>(out.size());
                            const auto a = static_cast<std::size_t>(std::clamp(t0 * fs, 0.0, static_cast<double>(s.size())));
                            const auto b = std::max(a + 1, static_cast<std::size_t>(std::clamp(t1 * fs, 0.0, static_cast<double>(s.size()))));
                            if (a >= s.size()) continue;
                            float lo = s[a], hi = s[a];
                            for (std::size_t i = a; i < std::min(b, s.size()); ++i) lo = std::min(lo, s[i]), hi = std::max(hi, s[i]);
                            out[c] = {lo, hi};
                        }
                        return out;
                    }};
}

}  // namespace

TempoScreen::TempoScreen(UiContext& ui, std::shared_ptr<SignalModel> signal) : ui_{ui}, signal_{std::move(signal)} {}

std::string TempoScreen::tab_label() const { return tr("tui.sub.tempo"); }

std::vector<Hint> TempoScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},  {"z", tr("tui.signal.hint.zoom")},      {"m", tr("tui.tempo.hint.mode")},
            {"+ −", tr("tui.tempo.hint.rate")},  {"> <", tr("tui.tempo.hint.semitones")}, {"w", tr("tui.tempo.hint.method")},
            {"s", tr("tui.tempo.hint.source")},  {"a/b", tr("tui.tempo.hint.play")},      {"Space", tr("tui.processing.hint.stop")},
            {"F1", tr("tui.tempo.hint.f1")}};
}

std::optional<TheoryLink> TempoScreen::theory_link() const { return TheoryLink{TheoryLink::Kind::card, "time_stretch"}; }

void TempoScreen::publish(theory::LiveValues& v) const {
    v.source = tab_label();
    v.set("block.mode", tr("tui.tempo.mode." + std::string{mode_id(options_.mode)}));
    v.set("block.rate", format_number(options_.rate, 3));
    v.set("block.semitones", format_number(options_.semitones, 3));
    v.set("block.method", std::string{core::stretch_method_name(options_.method)});
    v.set("block.n_fft", std::to_string(kTempoWindow));
    v.set("block.hop", std::to_string(kTempoHop));
    if (!view_) return;
    v.set("metric.duration", seconds(view_->seconds_in) + " → " + seconds(view_->seconds_out));
    if (!(view_->f0_in_median > 0 && view_->f0_out_median > 0)) return;
    v.set("metric.f0_in", hz(view_->f0_in_median));
    v.set("metric.f0_out", hz(view_->f0_out_median));
    v.set("metric.cents", format_fixed(cents_off(view_->f0_out_median, view_->expected), 2));
}

void TempoScreen::refresh() {
    const auto data = options_.source == TempoSource::file && signal_ ? signal_->data() : nullptr;
    const auto selection = data ? signal_->selection() : std::nullopt;
    if (computed_ && *computed_ == options_ && computed_data_ == data && computed_selection_ == selection && (view_ || !notice_.empty())) return;
    computed_ = options_;
    computed_data_ = data;
    computed_selection_ = selection;
    notice_.clear();
    std::vector<double> x;
    double fs = kTempoToneRate;
    if (options_.source == TempoSource::file) {
        if (!data || data->samples.empty() || data->samples.front().empty()) {
            notice_ = tr("tui.tempo.no_file");
            view_.reset();
            return;
        }
        const auto& s = data->samples.front();
        fs = data->info.sample_rate;
        const auto start = selection ? std::min(s.size() - 1, static_cast<std::size_t>(std::max(0.0, selection->first) * fs)) : std::size_t{0};
        const auto length = static_cast<std::size_t>(kTempoSeconds * fs);
        x.assign(s.begin() + static_cast<std::ptrdiff_t>(start), s.begin() + static_cast<std::ptrdiff_t>(std::min(s.size(), start + length)));
    } else {
        x = tempo_source(options_.source, fs, static_cast<std::size_t>(kTempoSeconds * fs));
    }
    auto v = compute_tempo(options_, x, fs);
    if (!v) {
        notice_ = error_text(v.error());
        view_.reset();
        return;
    }
    view_ = std::make_shared<const TempoView>(std::move(*v));
}

void TempoScreen::play(bool processed) {
    if (!ui_.player || !view_) return;
    audio::Clip c;
    c.rate = static_cast<std::uint32_t>(std::lround(view_->fs));
    c.owner = view_;
    c.channels.emplace_back(processed ? view_->y : view_->x);
    c.tag = processed ? "tempo.b" : "tempo.a";
    if (ui_.player->state() == audio::PlayState::playing && ui_.player->tag().starts_with("tempo")) {
        ui_.player->replace(std::move(c));
        return;
    }
    if (auto r = ui_.player->play(std::move(c), 0); !r) notice_ = fmt::format(fmt::runtime(tr("tui.signal.audio_unavailable")), fmt::arg("reason", r.error().message));
}

ftxui::Element TempoScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), ftxui::Box{0, -1, 0, -1}, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element TempoScreen::chip(std::string id, const std::string& label, bool on, std::function<void()> action) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto t = text(label);
    t = on ? t | bold | fg(th.bg, ui_) | bg(th.trace[0], ui_) : t | fg(th.fg, ui_);
    return hbox({clickable(std::move(id), t, std::move(action)), text(" ")});
}

ftxui::Element TempoScreen::frame(Panel p, std::string title, std::vector<Chip> chips, ftxui::Element body) {
    static constexpr const char* kNames[]{"controls", "before", "after", "readouts", "wave"};
    const std::string name = kNames[static_cast<std::size_t>(p)];
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

ftxui::Element TempoScreen::controls(int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& o = options_;
    auto set = [this](auto f) { return [this, f] { f(options_); }; };
    Elements first{notes_.mark("mode", text(tr("tui.tempo.mode") + " ") | fg(th.dim, ui_))};
    for (const auto m : {TempoMode::stretch, TempoMode::shift, TempoMode::speed})
        first.push_back(chip("mode_" + std::string{mode_id(m)}, tr("tui.tempo.mode." + std::string{mode_id(m)}), o.mode == m, set([m](TempoOptions& p) { p.mode = m; })));
    if (o.mode == TempoMode::shift) {
        first.push_back(notes_.mark("semitones", text(" " + semitones_label(o.semitones) + " ") | bold | fg(th.fg, ui_)));
        first.push_back(chip("semitones_less", "<", false, set([](TempoOptions& p) { p.semitones = std::max(-core::kMaxShiftSemitones, p.semitones - 1); })));
        first.push_back(chip("semitones_more", ">", false, set([](TempoOptions& p) { p.semitones = std::min(core::kMaxShiftSemitones, p.semitones + 1); })));
    } else {
        first.push_back(notes_.mark("rate", text(" ×" + format_number(o.rate, 3) + " ") | bold | fg(th.fg, ui_)));
        first.push_back(chip("rate_less", "−", false, set([](TempoOptions& p) { p.rate = next_tempo_rate(p.rate, false); })));
        first.push_back(chip("rate_more", "+", false, set([](TempoOptions& p) { p.rate = next_tempo_rate(p.rate, true); })));
    }
    Elements second;
    if (o.mode != TempoMode::speed) {
        second.push_back(notes_.mark("method", text(tr("tui.tempo.method") + " ") | fg(th.dim, ui_)));
        for (const auto m : {core::StretchMethod::vocoder, core::StretchMethod::locked, core::StretchMethod::wsola}) {
            const std::string id{core::stretch_method_name(m)};
            second.push_back(chip("method_" + id, tr("tui.tempo.method." + id), o.method == m, set([m](TempoOptions& p) { p.method = m; })));
        }
    }
    second.push_back(notes_.mark("source", text(tr("tui.tempo.source") + " ") | fg(th.dim, ui_)));
    for (const auto s : {TempoSource::tone, TempoSource::vibrato, TempoSource::file}) {
        const std::string id{source_id(s)};
        second.push_back(chip("source_" + id, tr("tui.tempo.source." + id), o.source == s, set([s](TempoOptions& p) { p.source = s; })));
    }
    second.push_back(notes_.mark("play", text(tr("tui.tempo.play") + " ") | fg(th.dim, ui_)));
    second.push_back(chip("play_a", "a " + tr("tui.tempo.play.a"), false, [this] { play(false); }));
    second.push_back(chip("play_b", "b " + tr("tui.tempo.play.b"), false, [this] { play(true); }));
    Elements body{flexbox(std::move(first)), flexbox(std::move(second))};
    if (!notice_.empty()) body.push_back(paragraph(notice_) | fg(th.warn, ui_));
    std::string title = tr("tui.tempo.title");
    if (view_) title += " · " + seconds(view_->seconds_in) + " → " + seconds(view_->seconds_out);
    return frame(controls_panel, ellipsize(title, std::max(8, width - 6), ui_.caps.glyphs == GlyphMode::ascii), {}, vbox(std::move(body)));
}

Axis TempoScreen::pitch_axis() const {
    double lo = std::numeric_limits<double>::infinity(), hi = 0;
    if (view_)
        for (const auto* track : {&view_->f0_in, &view_->f0_out})
            for (const double f : *track)
                if (f > 0) lo = std::min(lo, f), hi = std::max(hi, f);
    if (!(hi > 0)) return {50, 1000, AxisScale::log10};
    const double margin = std::pow(2.0, 2.0 / 12);
    return {lo / margin, hi * margin, AxisScale::log10};
}

ftxui::Element TempoScreen::track_body(bool after) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return text("—") | fg(th.dim, ui_);
    const auto& v = *view_;
    const char* id = after ? "after" : "before";
    PlotSpec spec;
    spec.x = zoom_.axis(id, {0, std::max(1e-3, after ? v.seconds_out : v.seconds_in)}).axis();
    spec.y = pitch_axis();
    spec.traces.push_back(pitch_trace(after ? v.f0_out : v.f0_in, v.frame_seconds, v.hop_seconds, after ? th.trace[0] : th.trace[1]));
    if (const double m = after ? v.f0_out_median : v.f0_in_median; m > 0) spec.y_marks.push_back(m);
    if (after && v.expected > 0) spec.y_marks.push_back(v.expected);
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
    return zoom_.track(id, std::move(spec), std::move(drawn), {tr(after ? "tui.tempo.after" : "tui.tempo.before"), {}, tr("tui.export.time_s"), tr("tui.export.freq_hz")});
}

ftxui::Element TempoScreen::readouts_body(int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return text("");
    const auto& v = *view_;
    const int room = std::max(10, width);
    Elements rows;
    rows.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.tempo.duration")), fmt::arg("before", seconds(v.seconds_in)), fmt::arg("after", seconds(v.seconds_out)),
                                         fmt::arg("ratio", format_number(v.seconds_in > 0 ? v.seconds_out / v.seconds_in : 0, 3)))) |
                   fg(th.fg, ui_));
    if (v.f0_in_median > 0 && v.f0_out_median > 0) {
        const double off = cents_off(v.f0_out_median, v.expected);
        rows.push_back(paragraph(fmt::format(fmt::runtime(tr("tui.tempo.pitch")), fmt::arg("before", hz(v.f0_in_median)), fmt::arg("after", hz(v.f0_out_median)),
                                             fmt::arg("expected", hz(v.expected)), fmt::arg("cents", format_fixed(off, 1)))) |
                       fg(std::abs(off) < 5 ? th.ok : th.warn, ui_));
    } else {
        rows.push_back(paragraph(tr("tui.tempo.unvoiced")) | fg(th.warn, ui_));
    }
    rows.push_back(paragraph(tr("tui.tempo.law." + std::string{mode_id(options_.mode)})) | fg(th.dim, ui_));
    if (options_.mode != TempoMode::speed)
        rows.push_back(paragraph(tr("tui.tempo.method_note." + std::string{core::stretch_method_name(options_.method)})) | fg(th.dim, ui_));
    return vbox(std::move(rows)) | size(WIDTH, LESS_THAN, room + 1);
}

ftxui::Element TempoScreen::wave_body() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!view_) return text("");
    const auto& v = *view_;
    PlotSpec spec;
    spec.x = zoom_.axis("wave", {0, std::max(1e-3, std::max(v.seconds_in, v.seconds_out))}).axis();
    float peak = 1e-6f;
    for (const auto* s : {&v.x, &v.y})
        for (const float f : *s) peak = std::max(peak, std::abs(f));
    spec.y = {-static_cast<double>(peak) * 1.05, static_cast<double>(peak) * 1.05};
    spec.envelopes.push_back(wave(view_, false, spec.x, th.dim));
    spec.envelopes.push_back(wave(view_, true, spec.x, th.trace[0]));
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
    return zoom_.track("wave", std::move(spec), std::move(drawn), {tr("tui.tempo.wave"), {}, tr("tui.export.time_s"), ""});
}

ftxui::Element TempoScreen::render_panel(Panel p, int width) {
    switch (p) {
    case controls_panel: return controls(width);
    case before_panel: return frame(p, tr("tui.tempo.before"), {{tr("tui.tempo.legend.median"), false, notes_.box("legend.median")}}, track_body(false));
    case after_panel: return frame(p, tr("tui.tempo.after"), {{tr("tui.tempo.legend.expected"), false, notes_.box("legend.expected")}}, track_body(true));
    case readouts_panel: return frame(p, tr("tui.tempo.readouts"), {}, readouts_body(width - 4));
    case wave_panel: return frame(p, tr("tui.tempo.wave"), {}, wave_body());
    case panel_count: break;
    }
    return ftxui::text("");
}

ftxui::Element TempoScreen::OnRender() {
    using namespace ftxui;
    clickables_.clear();
    notes_.clear();
    panel_boxes_.fill(kNoBox);
    zoom_.begin_frame();
    refresh();
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const std::vector<Panel> shown{before_panel, after_panel, readouts_panel, wave_panel};
    if (maximized_) {
        focus_ = *maximized_;
        return render_panel(*maximized_, cols) | flex;
    }
    if (breakpoint_ <= Breakpoint::small) {
        small_panels_ = shown;
        const auto p = shown[layout_.small_index() % shown.size()];
        if (focus_ != controls_panel) focus_ = p;
        return vbox({controls(cols), render_panel(p, cols) | flex});
    }
    small_panels_.clear();
    const bool wide = cols >= 120;
    auto plots = wide ? hbox({render_panel(before_panel, cols / 2) | flex, text(" "), render_panel(after_panel, cols / 2) | flex})
                      : vbox({render_panel(before_panel, cols) | flex, render_panel(after_panel, cols) | flex});
    const int readouts_width = wide ? cols / 2 : cols;
    auto bottom = wide ? hbox({render_panel(readouts_panel, readouts_width) | size(WIDTH, EQUAL, readouts_width), text(" "), render_panel(wave_panel, cols - readouts_width - 1) | flex})
                       : render_panel(readouts_panel, cols);
    return vbox({controls(cols), std::move(plots) | flex, std::move(bottom)});
}

std::vector<MenuItem> TempoScreen::context_menu(int x, int y) {
    if (auto items = zoom_.menu(x, y, ui_); !items.empty()) return items;
    return {};
}

std::vector<JumpTarget> TempoScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
}

std::optional<std::string> TempoScreen::explain_at(int x, int y) const {
    for (const auto& c : clickables_)
        if (valid(c.box) && c.box.Contain(x, y)) return notes_.explain(c.id);
    return notes_.at(x, y);
}

ftxui::Box TempoScreen::area(std::string_view id) const {
    if (id == "before_plot") return zoom_.box("before");
    if (id == "after_plot") return zoom_.box("after");
    if (id == "wave_plot") return zoom_.box("wave");
    static constexpr std::pair<std::string_view, Panel> kPanels[]{
        {"controls_panel", controls_panel}, {"before_panel", before_panel}, {"after_panel", after_panel}, {"readouts_panel", readouts_panel}, {"wave_panel", wave_panel}};
    for (const auto& [name, p] : kPanels)
        if (id == name) return panel_boxes_[p];
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

bool TempoScreen::OnEvent(ftxui::Event e) {
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
        if (breakpoint_ <= Breakpoint::small && !maximized_) {
            layout_.next_panel(std::max<std::size_t>(1, small_panels_.size()), back);
            focus_ = before_panel;
            return true;
        }
        focus_ = static_cast<Panel>((focus_ + (back ? panel_count - 1 : 1)) % panel_count);
        if (maximized_) maximized_ = focus_;
        return true;
    }
    if (e == Event::Character('0')) {
        const char* id = focus_ == before_panel ? "before" : focus_ == after_panel ? "after" : focus_ == wave_panel ? "wave" : nullptr;
        if (id) return zoom_.on_key(e, id);
    }
    auto& o = options_;
    if (e == Event::Character('m')) return o.mode = o.mode == TempoMode::stretch ? TempoMode::shift : o.mode == TempoMode::shift ? TempoMode::speed : TempoMode::stretch, true;
    if (e == Event::Character('+') || e == Event::Character('=')) return o.rate = next_tempo_rate(o.rate, true), true;
    if (e == Event::Character('-')) return o.rate = next_tempo_rate(o.rate, false), true;
    if (e == Event::Character('>') || e == Event::Character('.')) return o.semitones = std::min(core::kMaxShiftSemitones, o.semitones + 1), true;
    if (e == Event::Character('<') || e == Event::Character(',')) return o.semitones = std::max(-core::kMaxShiftSemitones, o.semitones - 1), true;
    if (e == Event::Character('w')) {
        o.method = o.method == core::StretchMethod::vocoder ? core::StretchMethod::locked : o.method == core::StretchMethod::locked ? core::StretchMethod::wsola : core::StretchMethod::vocoder;
        return true;
    }
    if (e == Event::Character('s')) {
        o.source = o.source == TempoSource::tone ? TempoSource::vibrato : o.source == TempoSource::vibrato ? TempoSource::file : TempoSource::tone;
        return true;
    }
    if (e == Event::Character('a')) return play(false), true;
    if (e == Event::Character('b')) return play(true), true;
    if (e == Event::Character(' ') && ui_.player) return ui_.player->stop(), true;
    return false;
}

std::shared_ptr<TabScreen> make_tempo_screen(UiContext& ui, std::shared_ptr<SignalModel> signal) { return std::make_shared<TempoScreen>(ui, std::move(signal)); }

}  // namespace signals::tui
