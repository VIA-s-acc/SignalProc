#include "tui/screens/spectrum_screen.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "i18n/i18n.hpp"
#include "tui/shell/plot_export.hpp"
#include "tui/theme/theme.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kRightWidth = 39;
constexpr int kFreqWidth = 7;
constexpr double kWheelStep = 1.25;
constexpr double kFmin = 30;
constexpr std::size_t kMinN = 256, kMaxN = 16384;

struct Overlay {
    std::string ch;
    Rgb fg;
};

class SpectrogramNode : public ftxui::Node {
public:
    SpectrogramNode(std::vector<double> db, std::size_t rows, std::size_t cols, double floor, bool grey, std::vector<std::optional<Overlay>> marks,
                    Theme theme, ColorDepth depth, GlyphMode glyphs)
        : db_{std::move(db)}, rows_{rows}, cols_{cols}, floor_{floor}, grey_{grey}, marks_{std::move(marks)}, theme_{std::move(theme)}, depth_{depth},
          glyphs_{glyphs} {}

    void ComputeRequirement() override {
        requirement_.min_x = 1;
        requirement_.min_y = 1;
        requirement_.flex_grow_x = requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_x = requirement_.flex_shrink_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        const int W = std::min<int>(box_.x_max - box_.x_min + 1, static_cast<int>(cols_));
        const int H = std::min<int>(box_.y_max - box_.y_min + 1, static_cast<int>(rows_ / 2));
        const bool shaded = glyphs_ == GlyphMode::ascii || depth_ == ColorDepth::ansi16 || depth_ == ColorDepth::none;
        static constexpr std::array<const char*, 10> ramp{" ", ".", ":", "-", "=", "+", "*", "#", "%", "@"};
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                auto& cell = screen.CellAt(box_.x_min + x, box_.y_min + y);
                const auto top = level(static_cast<std::size_t>(2 * y), static_cast<std::size_t>(x));
                const auto bottom = level(static_cast<std::size_t>(2 * y + 1), static_cast<std::size_t>(x));
                const auto& mark = marks_[static_cast<std::size_t>(y) * cols_ + static_cast<std::size_t>(x)];
                if (shaded) {
                    cell.character = mark ? mark->ch : ramp[static_cast<std::size_t>(std::lround((top + bottom) / 2 * 9))];
                    cell.foreground_color = to_color(mark ? mark->fg : theme_.fg, depth_);
                    continue;
                }
                if (mark) {
                    cell.character = mark->ch;
                    cell.foreground_color = to_color(mark->fg, depth_);
                    cell.background_color = to_color(colour((top + bottom) / 2), depth_);
                    continue;
                }
                cell.character = "▀";
                cell.foreground_color = to_color(colour(top), depth_);
                cell.background_color = to_color(colour(bottom), depth_);
            }
    }

private:
    double level(std::size_t row, std::size_t col) const {
        const double v = db_[std::min(row, rows_ - 1) * cols_ + col];
        return std::clamp((v - floor_) / -floor_, 0.0, 1.0);
    }
    Rgb colour(double t) const {
        if (!grey_) return viridis(t);
        const auto v = static_cast<std::uint8_t>(std::lround(255 * t));
        return {v, v, v};
    }

    std::vector<double> db_;
    std::size_t rows_, cols_;
    double floor_;
    bool grey_;
    std::vector<std::optional<Overlay>> marks_;
    Theme theme_;
    ColorDepth depth_;
    GlyphMode glyphs_;
};

std::string level_text(double db) { return std::isfinite(db) ? format_fixed(db, 0) + " " + tr("unit.db") : "—"; }

std::string note_text(double f) {
    const auto n = core::note_of(f);
    return n.name + " " + (n.cents >= 0 ? "+" : "−") + std::to_string(std::abs(n.cents)) + " ¢";
}

}  // namespace

SpectrumScreen::SpectrumScreen(UiContext& ui, std::shared_ptr<SpectrumModel> model) : ui_{ui}, model_{std::move(model)} { panel_boxes_.fill(kNoBox); }

std::string SpectrumScreen::tab_label() const { return tr("tui.sub.spectrogram"); }

std::vector<Hint> SpectrumScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},       {"z", tr("tui.signal.hint.zoom")},   {tr("tui.spectrum.key.click"), tr("tui.spectrum.hint.cursor")},
            {tr("tui.spectrum.key.drag"), tr("tui.spectrum.hint.box")}, {"Ctrl+" + tr("tui.spectrum.key.drag"), tr("tui.spectrum.hint.select")},
            {"w/W", tr("tui.spectrum.hint.window")},  {"o", tr("tui.spectrum.hint.overlap")}, {"[ ]", tr("tui.spectrum.hint.floor")},
            {"l", tr("tui.spectrum.hint.axis")},      {"C", tr("tui.spectrum.hint.palette")}, {"h", tr("tui.spectrum.hint.harmonics")},
            {"m", tr("tui.spectrum.hint.fold")},      {"+ − 0 < >", tr("tui.hint.plot_zoom")}, {"F1", tr("tui.spectrum.hint.f1")}};
}

std::optional<TheoryLink> SpectrumScreen::theory_link() const { return TheoryLink{TheoryLink::Kind::card, "stft"}; }

void SpectrumScreen::publish(theory::LiveValues& v) const {
    const auto s = model_->spectrogram();
    const auto src = model_->source();
    v.source = tab_label() + " ▸ " + (src ? src->path.filename().string() : std::string{"—"});
    if (!s) return;
    v.set("block.n", group(s->options.n));
    v.set("block.hop", group(s->hop));
    v.set("block.overlap", format_fixed(100.0 * (1.0 - static_cast<double>(s->hop) / static_cast<double>(s->options.n)), 1) + " %");
    v.set("block.rate", format_si(s->fs, tr("unit.hz")));
    v.set("metric.dt", format_si(static_cast<double>(s->options.n) / s->fs, tr("unit.s")));
    v.set("metric.df", format_si(s->fs / static_cast<double>(s->options.n), tr("unit.hz")));
    v.set("metric.frames", group(s->cols));
}

std::string SpectrumScreen::context() const {
    const auto s = model_->source();
    if (!s) return {};
    const auto ch = model_->options().channel;
    const auto channel = ch < 0 ? tr("tui.decomposition.mono") : ch == 0 && s->samples.size() == 2 ? std::string{"L"} : ch == 1 && s->samples.size() == 2 ? std::string{"R"}
                                                                                                                                    : fmt::format(fmt::runtime(tr("tui.decomposition.channel")), fmt::arg("n", ch + 1));
    return fmt::format("{} · {} · {} · {}", s->path.filename().string(), channel, format_si(s->info.sample_rate, tr("unit.hz")),
                       format_fixed(s->duration(), 2) + " " + tr("unit.s"));
}

ftxui::Element SpectrumScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    static constexpr const char* kNames[]{"spectrogram", "selection", "cursor", "window", "measure", "list", "theory"};
    const std::string name = kNames[static_cast<std::size_t>(p)];
    for (auto& c : chips) c.box = notes_.box("legend." + name);
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1), fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_, notes_.box("panel." + name)) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element SpectrumScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), kNoBox, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

bool SpectrumScreen::log_view() const { return log_ && nyquist() > 2 * kFmin; }

double SpectrumScreen::nyquist() const {
    const auto s = model_->source();
    return s ? s->info.sample_rate / 2.0 : 22050;
}

void SpectrumScreen::set_n(std::size_t n) {
    auto o = model_->options();
    o.n = std::clamp(n, kMinN, kMaxN);
    if (ui_.jobs) model_->set_options(o, *ui_.jobs);
}

ftxui::Element SpectrumScreen::toolbar(bool two_rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto chip = [&](std::string id, std::string label, bool on, std::function<void()> action) {
        auto e = text(on ? "[" + label + "]" : " " + label + " ") | fg(on ? th.accent : th.fg, ui_);
        if (on) e = e | bold;
        return clickable(std::move(id), e, std::move(action));
    };
    const auto o = model_->options();
    const auto overlap = format_fixed(100.0 * (1.0 - 1.0 / static_cast<double>(o.hop_divisor)), o.hop_divisor == 8 ? 1 : 0);
    Elements parts{notes_.mark("window", text(tr("tui.spectrum.window") + " ") | fg(th.dim, ui_)),
                   chip("preset_wide", tr("tui.spectrum.wide") + " · N 512", o.n == 512, [this] { set_n(512); }),
                   chip("preset_narrow", tr("tui.spectrum.narrow") + " · N 4096", o.n == 4096, [this] { set_n(4096); }),
                   notes_.mark("hann", text(" │ " + tr("tui.spectrum.hann") + " · " + overlap + " %") | fg(th.fg, ui_)),
                   notes_.mark("axis", text(" │ " + tr("tui.spectrum.axis") + " ") | fg(th.dim, ui_)),
                   chip("axis_log", tr("tui.spectrum.log"), log_, [this] { log_ = true; }),
                   chip("axis_lin", tr("tui.spectrum.lin"), !log_, [this] { log_ = false; })};
    Elements second{text((two_rows ? "" : " │ ")) | fg(th.dim, ui_), chip("harmonics", tr("tui.spectrum.harmonics"), harmonics_, [this] { harmonics_ = !harmonics_; }),
                    notes_.mark("fold", text(" │ " + tr("tui.spectrum.fold") + " ") | fg(th.dim, ui_))};
    for (const std::size_t m : {std::size_t{0}, std::size_t{2}, std::size_t{4}})
        second.push_back(chip("fold_" + std::to_string(m), m ? "↓" + std::to_string(m) : tr("tui.spectrum.fold_none"), fold_ == m, [this, m] { fold_ = m; }));
    second.push_back(notes_.mark("floor", text(" │ " + tr("tui.spectrum.floor") + " " + format_fixed(floor_db_, 0) + " " + tr("unit.db")) | fg(th.fg, ui_)));
    if (two_rows) return vbox({hbox(std::move(parts)), hbox(std::move(second))});
    for (auto& e : second) parts.push_back(std::move(e));
    return hbox(std::move(parts));
}

ftxui::Element SpectrumScreen::time_axis(int width, std::optional<std::pair<double, double>> selection) const {
    std::vector<std::string> cells(static_cast<std::size_t>(std::max(0, width)), " ");
    std::vector<bool> selected(cells.size(), false);
    if (selection)
        for (std::size_t c = 0; c < cells.size(); ++c) {
            const double t = begin_ + (static_cast<double>(c) + 0.5) / std::max(1, width) * (end_ - begin_);
            selected[c] = t >= selection->first && t <= selection->second;
        }
    const auto ticks = nice_ticks(begin_, end_, std::max(2, width / 14));
    int next_free = 0;
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        const auto label = ticks[i].label + (i + 1 == ticks.size() ? " " + tr("unit.s") : "");
        const auto cps = split_codepoints(label);
        int at = static_cast<int>(std::lround((ticks[i].value - begin_) / std::max(1e-12, end_ - begin_) * (width - 1)));
        at = std::clamp(at - static_cast<int>(cps.size()) / 2, 0, std::max(0, width - static_cast<int>(cps.size())));
        if (at < next_free) continue;
        for (std::size_t k = 0; k < cps.size() && at + static_cast<int>(k) < width; ++k) cells[static_cast<std::size_t>(at) + k] = cps[k];
        next_free = at + static_cast<int>(cps.size()) + 1;
    }
    ftxui::Elements parts;
    std::string run;
    bool run_selected = false;
    auto flush = [&] {
        if (run.empty()) return;
        parts.push_back(ftxui::text(run) | fg(run_selected ? ui_.theme.accent : ui_.theme.dim, ui_) | (run_selected ? ftxui::underlined : ftxui::nothing));
        run.clear();
    };
    for (std::size_t c = 0; c < cells.size(); ++c) {
        if (selected[c] != run_selected) {
            flush();
            run_selected = selected[c];
        }
        run += cells[c] == " " && selected[c] ? (ui_.caps.glyphs == GlyphMode::ascii ? "=" : "━") : cells[c];
    }
    flush();
    return ftxui::hbox(std::move(parts));
}

double SpectrumScreen::time_at(int x) const {
    const int width = std::max(1, plot_box_.x_max - plot_box_.x_min + 1);
    return std::clamp(begin_ + (static_cast<double>(x - plot_box_.x_min) + 0.5) / width * (end_ - begin_), begin_, end_);
}

double SpectrumScreen::freq_at(int y) const {
    const int height = std::max(1, plot_box_.y_max - plot_box_.y_min + 1);
    const double u = std::clamp(1.0 - (static_cast<double>(y - plot_box_.y_min) + 0.5) / height, 0.0, 1.0);
    const double nyq = nyquist();
    return log_view() ? kFmin * std::pow(nyq / kFmin, u) : nyq * u;
}

ftxui::Element SpectrumScreen::spectrogram_body(const SpectrogramData& s, const SelectionSpectrum* sel, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const int plot_width = std::max(10, width - kFreqWidth - 4);
    const int H = std::max(2, height);
    GridSpec g;
    g.rows = static_cast<std::size_t>(2 * H);
    g.cols = static_cast<std::size_t>(plot_width);
    g.log = log_view();
    g.fmin = kFmin;
    g.floor_db = floor_db_;
    g.t0 = begin_;
    g.t1 = end_;
    if (grid_for_.get() != &s || !(grid_spec_ == g)) {
        grid_ = spectrogram_grid(s, g);
        grid_spec_ = g;
        grid_for_ = model_->spectrogram();
    }
    auto grid = grid_;
    const double nyq = s.fs / 2;
    auto row_of = [&](double f) -> std::optional<int> {
        if (f <= 0 || f > nyq || (log_view() && f < kFmin)) return std::nullopt;
        const double u = log_view() ? std::log(f / kFmin) / std::log(nyq / kFmin) : f / nyq;
        return std::clamp(static_cast<int>(std::floor((1 - u) * H)), 0, H - 1);
    };
    auto col_of = [&](double t) -> std::optional<int> {
        if (t < begin_ || t > end_) return std::nullopt;
        return std::clamp(static_cast<int>(std::floor((t - begin_) / std::max(1e-12, end_ - begin_) * plot_width)), 0, plot_width - 1);
    };
    std::vector<std::optional<Overlay>> marks(static_cast<std::size_t>(H) * g.cols);
    auto put = [&](int x, int y, std::string ch, Rgb colour) {
        if (x < 0 || y < 0 || x >= plot_width || y >= H) return;
        marks[static_cast<std::size_t>(y) * g.cols + static_cast<std::size_t>(x)] = Overlay{std::move(ch), colour};
    };
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    if (fold_ > 0)
        if (const auto r = row_of(s.fs / (2.0 * static_cast<double>(fold_)))) {
            for (int x = 0; x < plot_width; ++x) put(x, *r, ascii ? "-" : "─", th.warn);
            const auto label = " " + fmt::format(fmt::runtime(tr("tui.spectrum.fold_note")), fmt::arg("m", fold_),
                                                 fmt::arg("f", hz_text(s.fs / (2.0 * static_cast<double>(fold_))) + " " + tr("unit.hz"))) + " ";
            int x = 1;
            for (const auto& cp : split_codepoints(label)) put(x++, *r, cp, th.warn);
        }
    if (harmonics_ && sel && sel->f0) {
        const auto c0 = col_of(sel->t0).value_or(sel->t0 < begin_ ? 0 : plot_width);
        const auto c1 = col_of(sel->t1).value_or(sel->t1 > end_ ? plot_width - 1 : -1);
        for (int k = 1; k <= 8; ++k)
            if (const auto r = row_of(k * *sel->f0))
                for (int x = c0; x <= c1; x += 4) put(x, *r, ascii ? "." : "·", th.accent);
    }
    auto outline = [&](int x0, int y0, int x1, int y1, Rgb colour) {
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        for (int x = x0; x <= x1; ++x) {
            put(x, y0, ascii ? "-" : "─", colour);
            put(x, y1, ascii ? "-" : "─", colour);
        }
        for (int y = y0; y <= y1; ++y) {
            put(x0, y, ascii ? "|" : "│", colour);
            put(x1, y, ascii ? "|" : "│", colour);
        }
        put(x0, y0, ascii ? "+" : "┌", colour);
        put(x1, y0, ascii ? "+" : "┐", colour);
        put(x0, y1, ascii ? "+" : "└", colour);
        put(x1, y1, ascii ? "+" : "┘", colour);
    };
    if (measure_ && measure_->t1 >= begin_ && measure_->t0 <= end_) {
        const auto x0 = col_of(std::clamp(measure_->t0, begin_, end_)), x1 = col_of(std::clamp(measure_->t1, begin_, end_));
        const auto y0 = row_of(std::clamp(measure_->f1, log_view() ? kFmin : 1e-9, nyq)), y1 = row_of(std::clamp(measure_->f0, log_view() ? kFmin : 1e-9, nyq));
        if (x0 && x1 && y0 && y1) outline(*x0, *y0, *x1, *y1, th.fg);
    }
    if (cursor_) {
        const auto [t, f] = *cursor_;
        const double dt = static_cast<double>(s.options.n) / s.fs, df = s.fs / static_cast<double>(s.options.n);
        const auto x = col_of(t), y = row_of(f);
        const auto xa = col_of(std::max(begin_, t - dt / 2)), xb = col_of(std::min(end_, t + dt / 2));
        const auto ya = row_of(std::min(nyq, f + df / 2)), yb = row_of(std::max(log_view() ? kFmin : 1e-9, f - df / 2));
        if (xa && xb && ya && yb && *xb - *xa >= 2 && *yb - *ya >= 2) outline(*xa, *ya, *xb, *yb, th.fg);
        if (x && y) put(*x, *y, ascii ? "+" : "┼", th.fg);
    }
    Elements labels(static_cast<std::size_t>(H), text(""));
    std::vector<bool> used(static_cast<std::size_t>(H), false);
    const std::vector<double> log_marks{20000, 10000, 5000, 2000, 1000, 500, 200, 100, 50};
    std::vector<double> ticks;
    if (log_view()) ticks = log_marks;
    else
        for (const auto& t : nice_ticks(0, nyq, std::max(2, H / 3))) ticks.push_back(t.value);
    for (const double f : ticks)
        if (const auto r = row_of(f); r && !used[static_cast<std::size_t>(*r)] && (*r == 0 || !used[static_cast<std::size_t>(*r - 1)])) {
            labels[static_cast<std::size_t>(*r)] = text(pad_left(hz_text(f), kFreqWidth - 1)) | fg(th.dim, ui_);
            used[static_cast<std::size_t>(*r)] = true;
        }
    const auto rows = g.rows, cols = g.cols;
    Element map = std::make_shared<SpectrogramNode>(std::move(grid), rows, cols, floor_db_, grey_, std::move(marks), th, ui_.caps.color, ui_.caps.glyphs);
    plot_box_ = kNoBox;
    return vbox({hbox({vbox(std::move(labels)) | size(WIDTH, EQUAL, kFreqWidth), std::move(map) | size(WIDTH, EQUAL, plot_width) | size(HEIGHT, EQUAL, H) | reflect(plot_box_)}),
                 hbox({text(std::string(kFreqWidth, ' ')), time_axis(plot_width, sel ? std::optional{std::pair{sel->t0, sel->t1}} : std::nullopt)})});
}

ftxui::Element SpectrumScreen::spectrum_body(const SelectionSpectrum& sel, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    PlotSpec spec;
    const double nyq = nyquist();
    spec.x = zoom_.axis("selection", log_view() ? Axis{20, nyq, AxisScale::log10} : Axis{0, nyq, AxisScale::linear}).axis();
    spec.y = {-100, 0};
    Trace t;
    t.color = th.trace[0];
    const auto dots = static_cast<std::size_t>(2 * std::max(10, width - 4));
    std::vector<double> best(dots, -INFINITY), at(dots, 0.0);
    for (std::size_t i = 0; i < sel.f.size(); ++i) {
        if (log_view() && sel.f[i] < 20) continue;
        const double u = axis_fraction(spec.x, sel.f[i]);
        if (u < 0 || u > 1) continue;
        const auto c = std::min(dots - 1, static_cast<std::size_t>(u * static_cast<double>(dots)));
        if (sel.db[i] > best[c]) best[c] = sel.db[i], at[c] = sel.f[i];
    }
    for (std::size_t c = 0; c < dots; ++c)
        if (std::isfinite(best[c])) {
            t.x.push_back(at[c]);
            t.y.push_back(std::max(-100.0, best[c]));
        }
    spec.traces.push_back(std::move(t));
    if (harmonics_)
        for (const auto& h : sel.harmonics) spec.x_marks.push_back(h.f);
    else
        for (const auto& p : sel.peaks) spec.x_marks.push_back(sel.f[p.index]);
    if (fold_ > 0) spec.x_marks.push_back(nyq / static_cast<double>(fold_));
    if (cursor_) spec.cursor_a = cursor_->second;
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, std::max(3, height)) | size(WIDTH, EQUAL, std::max(10, width - 4));
    return zoom_.track("selection", std::move(spec), std::move(drawn), {tr("tui.spectrum.selection"), {}, tr("tui.export.freq_hz"), tr("tui.filters.db")});
}

ftxui::Element SpectrumScreen::cursor_panel_body(const SpectrogramData* s, const SelectionSpectrum* sel) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!cursor_ || !s) return paragraph(tr("tui.spectrum.cursor_hint")) | fg(th.dim, ui_);
    const auto [t, f] = *cursor_;
    auto row = [&](const std::string& key, const std::string& value) { return hbox({text(pad_right(tr(key), 11)) | fg(th.dim, ui_), text(value) | fg(th.fg, ui_)}); };
    Elements rows{row("tui.spectrum.time", format_fixed(t, 2) + " " + tr("unit.s") + " · " + tr("tui.spectrum.frame") + " " + group(static_cast<std::uint64_t>(std::llround(t * s->fs)))),
                  row("tui.spectrum.frequency", format_si(f, tr("unit.hz"))), row("tui.spectrum.note", note_text(f)),
                  row("tui.spectrum.level", level_text(level_at_db(*s, t, f)))};
    if (sel && !sel->peaks.empty()) {
        const auto* near = &sel->peaks.front();
        for (const auto& p : sel->peaks)
            if (std::abs(std::log(sel->f[p.index] / f)) < std::abs(std::log(sel->f[near->index] / f))) near = &p;
        const double pf = sel->f[near->index];
        rows.push_back(row("tui.spectrum.near_peak", format_si(pf, tr("unit.hz")) + " " + core::note_of(pf).name + " · " + format_fixed(sel->db[near->index], 1) + " " + tr("unit.db")));
    }
    return vbox(std::move(rows));
}

ftxui::Element SpectrumScreen::window_body(const SpectrogramData* s) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto o = model_->options();
    const double fs = s ? s->fs : nyquist() * 2;
    const auto hop = std::max<std::size_t>(1, o.n / o.hop_divisor);
    auto row = [&](const std::string& label, const std::string& value) { return hbox({text(pad_right(label, 11)) | fg(th.dim, ui_), text(value) | fg(th.fg, ui_)}); };
    return vbox({row(tr("tui.spectrum.hann") + " N", group(o.n) + " · " + tr("tui.spectrum.step") + " " + group(hop)),
                 row("Δt = N/fs", format_si(static_cast<double>(o.n) / fs, tr("unit.s"))), row("Δf = fs/N", format_si(fs / static_cast<double>(o.n), tr("unit.hz"))),
                 text(tr("tui.spectrum.heisenberg")) | fg(th.dim, ui_)});
}

ftxui::Element SpectrumScreen::measure_body(const SpectrogramData* s) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!measure_) return paragraph(tr("tui.spectrum.measure_hint")) | fg(th.dim, ui_);
    const auto& m = *measure_;
    auto row = [&](const std::string& label, const std::string& value) { return hbox({text(pad_right(label, 11)) | fg(th.dim, ui_), text(value) | fg(th.fg, ui_)}); };
    return vbox({row("t", format_fixed(m.t0, 2) + "–" + format_fixed(m.t1, 2) + " " + tr("unit.s") + " · Δt " + format_fixed(m.t1 - m.t0, 2) + " " + tr("unit.s")),
                 row("f", hz_text(m.f0) + "–" + hz_text(m.f1) + " " + tr("unit.hz") + " · Δf " + format_si(m.f1 - m.f0, tr("unit.hz"))),
                 row(tr("tui.spectrum.mean_level"), s ? level_text(box_level_db(*s, m.t0, m.t1, m.f0, m.f1)) : std::string{"—"})});
}

std::optional<ChartExport> SpectrumScreen::list_chart(const SelectionSpectrum& sel) const {
    ChartExport out{harmonics_ ? "harmonics" : "peaks", tr(harmonics_ ? "tui.export.harmonics" : "tui.export.peaks"), {}, {ui_.theme.trace[0]},
                    harmonics_ ? "k,f_hz,db\n" : "f_hz,db,note\n"};
    out.chart.title = out.title;
    svg::Series s{tr("tui.export.level_db")};
    s.stems = true;
    if (harmonics_) {
        for (const auto& h : sel.harmonics) {
            s.x.push_back(h.f);
            s.y.push_back(h.db);
            out.csv += fmt::format("{},{},{}\n", h.k, h.f, h.db);
        }
    } else {
        for (const auto& p : sel.peaks) {
            if (p.index >= sel.f.size() || p.index >= sel.db.size()) continue;
            s.x.push_back(sel.f[p.index]);
            s.y.push_back(sel.db[p.index]);
            out.csv += fmt::format("{},{},{}\n", sel.f[p.index], sel.db[p.index], core::note_of(sel.f[p.index]).name);
        }
    }
    if (s.x.empty()) return std::nullopt;
    const auto [x_lo, x_hi] = std::minmax_element(s.x.begin(), s.x.end());
    const auto [y_lo, y_hi] = std::minmax_element(s.y.begin(), s.y.end());
    const bool log = log_ && *x_lo > 0;
    out.chart.x = {log ? *x_lo / 1.5 : 0.0, log ? *x_hi * 1.5 : *x_hi * 1.05, log ? svg::Scale::log10 : svg::Scale::linear, tr("tui.export.freq_hz")};
    out.chart.y = {*y_lo - 6, *y_hi + 6, svg::Scale::linear, tr("tui.export.level_db")};
    out.chart.series.push_back(std::move(s));
    return out;
}

std::vector<ChartExport> SpectrumScreen::chart_exports() {
    if (shown_list_) return {*shown_list_};
    return {};
}

ftxui::Element SpectrumScreen::list_body(const SelectionSpectrum* sel, int rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!sel) return text(tr("tui.spectrum.computing")) | fg(th.dim, ui_);
    shown_list_ = list_chart(*sel);
    auto bar_of = [&](double db) { return bar(std::clamp((db + 30) / 30, 0.0, 1.0), 7, ui_.caps.glyphs); };
    Elements out;
    if (harmonics_) {
        for (const auto& h : sel->harmonics) {
            if (static_cast<int>(out.size()) >= rows) break;
            out.push_back(hbox({text(pad_right(std::to_string(h.k) + "·f₀", 6)) | fg(h.k == 1 ? th.accent : th.fg, ui_), text(pad_left(format_si(h.f, tr("unit.hz")), 11)) | fg(th.fg, ui_),
                                text(pad_left(format_fixed(h.db, 1) + " " + tr("unit.db"), 10)) | fg(th.fg, ui_), text(" " + bar_of(h.db)) | fg(th.trace[0], ui_)}));
        }
    } else {
        for (const auto& p : sel->peaks) {
            if (static_cast<int>(out.size()) >= rows) break;
            const double f = sel->f[p.index];
            out.push_back(hbox({text(pad_left(format_si(f, tr("unit.hz")), 10)) | fg(th.fg, ui_), text(" " + pad_right(core::note_of(f).name, 5)) | fg(th.dim, ui_),
                                text(pad_left(format_fixed(sel->db[p.index], 1) + " " + tr("unit.db"), 10)) | fg(th.fg, ui_), text(" " + bar_of(sel->db[p.index])) | fg(th.trace[0], ui_)}));
        }
    }
    if (out.empty()) out.push_back(text(tr("tui.spectrum.no_peaks")) | fg(th.dim, ui_));
    return vbox(std::move(out));
}

void SpectrumScreen::sync_view(const std::shared_ptr<const SpectrogramData>& s) {
    if (!s) return;
    if (!seen_ || seen_->source.lock() != s->source.lock()) {
        begin_ = 0;
        end_ = s->duration;
        cursor_.reset();
        measure_.reset();
    }
    seen_ = s;
    if (end_ <= begin_) end_ = begin_ + std::max(1e-3, s->duration);
}

void SpectrumScreen::zoom(double factor, double anchor) {
    const auto s = model_->source();
    const double total = s ? s->duration() : 0;
    if (total <= 0) return;
    const double width = std::clamp((end_ - begin_) / factor, std::min(total, 0.01), total);
    double b = anchor - (anchor - begin_) / (end_ - begin_) * width;
    b = std::clamp(b, 0.0, total - width);
    begin_ = b;
    end_ = b + width;
}

std::optional<HeatmapExport> SpectrumScreen::spectrogram_export() const {
    const auto s = model_->spectrogram();
    if (!s || !valid(plot_box_)) return std::nullopt;
    auto g = grid_spec_;
    g.rows = 256;
    g.cols = 800;
    HeatmapSpec spec;
    spec.rows = g.rows;
    spec.cols = g.cols;
    spec.values = spectrogram_grid(*s, g);
    spec.floor = floor_db_;
    spec.ceil = spec.values.empty() ? 0.0 : *std::max_element(spec.values.begin(), spec.values.end());
    const double nyq = s->fs / 2;
    return HeatmapExport{"spectrogram", tr("tui.export.spectrogram"), std::move(spec), Axis{g.t0, g.t1},
                         g.log ? Axis{g.fmin, nyq, AxisScale::log10} : Axis{0, nyq}, tr("tui.export.time_s"), tr("tui.export.freq_hz")};
}

std::vector<HeatmapExport> SpectrumScreen::heatmap_exports() {
    if (auto map = spectrogram_export()) return {std::move(*map)};
    return {};
}

std::vector<MenuItem> SpectrumScreen::context_menu(int x, int y) {
    if (auto items = zoom_.menu(x, y, ui_); !items.empty()) return items;
    const auto s = model_->source();
    const double total = s ? s->duration() : 0;
    std::vector<MenuItem> items;
    if (valid(plot_box_) && plot_box_.Contain(x, y) && total > 0 && (begin_ > 0 || end_ < total)) items.push_back({tr("tui.menu.reset_zoom"), "0", [this] { zoom(1e-12, begin_); }});
    if (valid(plot_box_) && plot_box_.Contain(x, y))
        if (auto map = spectrogram_export()) items.push_back(export_item(ui_, std::move(*map)));
    if (const auto& b = panel_boxes_[list_panel]; shown_list_ && valid(b) && b.Contain(x, y)) items.push_back(export_item(ui_, *shown_list_));
    return items;
}

void SpectrumScreen::pan(double dt) {
    const auto s = model_->source();
    const double total = s ? s->duration() : 0;
    const double width = end_ - begin_;
    begin_ = std::clamp(begin_ + dt, 0.0, std::max(0.0, total - width));
    end_ = begin_ + width;
}

ftxui::Element SpectrumScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    clickables_.clear();
    notes_.clear();
    zoom_.begin_frame();
    shown_list_.reset();
    if (!ui_.jobs) return text("");
    model_->sync(*ui_.jobs);
    const auto s = model_->spectrogram();
    const auto sel = model_->selection();
    sync_view(s);
    const auto& th = ui_.theme;
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const bool small = breakpoint_ <= Breakpoint::small || maximized_;
    const bool two_rows = cols < 150;
    const int total = std::max(8, rows - 3);
    const int toolbar_rows = two_rows ? 2 : 1;
    const int main_width = small ? cols : cols - kRightWidth - 1;
    const int spectrum_rows = small ? std::max(4, total / 3) : 7;
    const int spectrogram_rows = std::max(4, total - toolbar_rows - 1 - 3 - (small ? 0 : spectrum_rows + 3));
    const auto o = model_->options();
    const auto title = fmt::format(fmt::runtime(tr("tui.spectrum.title")), fmt::arg("channel", context().empty() ? std::string{"—"} : (o.channel == 0 ? std::string{"L"} : o.channel == 1 ? std::string{"R"} : tr("tui.decomposition.mono"))),
                                   fmt::arg("n", group(o.n)), fmt::arg("band", tr(o.n <= 1024 ? "tui.spectrum.wideband" : "tui.spectrum.narrowband")));
    const auto chip_text = tr(log_view() ? "tui.spectrum.chip_log" : "tui.spectrum.chip_lin");
    Element map_body;
    if (s) {
        map_body = spectrogram_body(*s, sel.get(), main_width, spectrogram_rows);
    } else {
        const auto e = model_->error();
        const bool stopped = model_->cancelled() && !model_->job();
        map_body = text(e ? *e : stopped ? tr("tui.spectrum.cancelled") : model_->source() ? tr("tui.spectrum.computing") : tr("tui.spectrum.empty")) |
                   fg(e ? th.err : stopped ? th.warn : th.dim, ui_);
        plot_box_ = kNoBox;
    }
    auto map_panel = frame_panel(spectrogram_panel, map_body, {{chip_text, false}}, title);
    const auto sel_title = sel ? fmt::format(fmt::runtime(tr("tui.spectrum.selection_title")), fmt::arg("t0", format_fixed(sel->t0, 2)), fmt::arg("t1", format_fixed(sel->t1, 2)),
                                             fmt::arg("n", group(std::min<std::size_t>(16384, static_cast<std::size_t>((sel->t1 - sel->t0) * (s ? s->fs : 0))))))
                               : tr("tui.spectrum.selection");
    const auto harm_chip = harmonics_ && sel && sel->f0 ? fmt::format(fmt::runtime(tr("tui.spectrum.harmonics_of")), fmt::arg("f", format_si(*sel->f0, tr("unit.hz")))) : tr("tui.spectrum.peaks_chip");
    auto spec_panel = frame_panel(spectrum_panel, sel ? spectrum_body(*sel, main_width, spectrum_rows) : text(tr("tui.spectrum.computing")) | fg(th.dim, ui_), {{harm_chip, false}}, sel_title);
    auto cursor_box = frame_panel(cursor_panel, cursor_panel_body(s.get(), sel.get()), {}, tr("tui.spectrum.under_cursor"));
    auto window_box = frame_panel(window_panel, window_body(s.get()), {{"w / W", false}}, tr("tui.spectrum.window_title"));
    auto measure_box = frame_panel(measure_panel, measure_body(s.get()), {{tr("tui.spectrum.drag_chip"), false}}, tr("tui.spectrum.measure"));
    const auto list_title = harmonics_ && sel && sel->f0 ? fmt::format(fmt::runtime(tr("tui.spectrum.harmonics_title")), fmt::arg("f", format_si(*sel->f0, tr("unit.hz"))))
                                                         : tr("tui.spectrum.peaks_title");
    const auto list_note = harmonics_ && sel && sel->f0 ? note_text(*sel->f0) : tr("tui.spectrum.relative");
    auto list_box = frame_panel(list_panel, list_body(sel.get(), 9), {{list_note, false}}, list_title);
    auto theory_box = frame_panel(theory_panel, paragraph(tr("tui.spectrum.theory_text")) | fg(th.dim, ui_), {{tr("tui.spectrum.theory_chip"), false}}, tr("tui.spectrum.theory"));
    if (small) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case spectrogram_panel: return vbox({toolbar(two_rows), map_panel | flex});
        case spectrum_panel: return spec_panel;
        case cursor_panel: return cursor_box;
        case window_panel: return window_box;
        case measure_panel: return measure_box;
        case list_panel: return list_box;
        case theory_panel: return theory_box;
        case panel_count: break;
        }
    }
    auto left = vbox({map_panel | flex, text(""), spec_panel});
    auto right = vbox({cursor_box, window_box, measure_box, list_box, theory_box | flex});
    return vbox({toolbar(two_rows), text(""), hbox({left | flex, text(" "), right | size(WIDTH, EQUAL, kRightWidth)}) | flex});
}

bool SpectrumScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    using ftxui::Mouse;
    if (!ui_.jobs) return false;
    auto& jobs = *ui_.jobs;
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        const bool inside = valid(plot_box_) && plot_box_.Contain(m.x, m.y);
        if (!inside && !drag_.active && zoom_.on_mouse(m)) return true;
        if ((m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) && inside) {
            zoom(m.button == Mouse::WheelUp ? kWheelStep : 1 / kWheelStep, time_at(m.x));
            return true;
        }
        if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
            for (auto& c : clickables_)
                if (c.box.Contain(m.x, m.y)) {
                    auto action = c.action;
                    action();
                    return true;
                }
            for (int p = 0; p < panel_count; ++p)
                if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) focus_ = static_cast<Panel>(p);
            if (!inside && zoom_.press(m.x, m.y)) return true;
            if (inside) {
                drag_ = {true, false, m.control, m.x, m.y};
                cursor_ = std::pair{time_at(m.x), freq_at(m.y)};
            }
            return true;
        }
        if (drag_.active) {
            const int x = valid(plot_box_) ? std::clamp(m.x, plot_box_.x_min, plot_box_.x_max) : m.x;
            const int y = valid(plot_box_) ? std::clamp(m.y, plot_box_.y_min, plot_box_.y_max) : m.y;
            if (x != drag_.x || y != drag_.y) drag_.moved = true;
            if (drag_.moved) {
                const bool box = valid(plot_box_);
                const double t1 = box && m.x > plot_box_.x_max ? end_ : box && m.x < plot_box_.x_min ? begin_ : time_at(x);
                const double f1 = box && m.y < plot_box_.y_min ? nyquist() : box && m.y > plot_box_.y_max ? (log_view() ? kFmin : 0.0) : freq_at(y);
                const double t0 = time_at(drag_.x), f0 = freq_at(drag_.y);
                if (drag_.select) model_->set_selection(std::min(t0, t1), std::max(t0, t1), jobs);
                else measure_ = Measure{std::min(t0, t1), std::max(t0, t1), std::min(f0, f1), std::max(f0, f1)};
            }
            if (m.motion == Mouse::Released) drag_.active = false;
            return true;
        }
        return false;
    }
    if (e == Event::Escape) {
        if (model_->job()) model_->cancel(jobs);
        else if (measure_) measure_.reset();
        else if (const auto sel = model_->selection(); sel && sel->t0 > 0) model_->clear_selection(jobs);
        else if (maximized_) maximized_.reset();
        else return false;
        return true;
    }
    if (e == Event::Character('r')) return model_->recompute(jobs), true;
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
    if (e == Event::Character('w')) return set_n(model_->options().n * 2), true;
    if (e == Event::Character('W')) return set_n(model_->options().n / 2), true;
    if (e == Event::Character('o')) {
        auto o = model_->options();
        o.hop_divisor = o.hop_divisor == 4 ? 8 : o.hop_divisor == 8 ? 2 : 4;
        model_->set_options(o, jobs);
        return true;
    }
    if (e == Event::Character('[')) return floor_db_ = std::max(-150.0, floor_db_ - 10), true;
    if (e == Event::Character(']')) return floor_db_ = std::min(-30.0, floor_db_ + 10), true;
    if (e == Event::Character('C')) return grey_ = !grey_, true;
    if (e == Event::Character('h')) return harmonics_ = !harmonics_, true;
    if (e == Event::Character('l')) return log_ = !log_, true;
    if (e == Event::Character('m')) return fold_ = fold_ == 0 ? 2 : fold_ == 2 ? 4 : 0, true;
    if (focus_ == spectrum_panel && zoom_.on_key(e, "selection")) return true;
    if (e == Event::Character('+') || e == Event::Character('=') || e == Event::Character('-')) {
        const double anchor = cursor_ && cursor_->first >= begin_ && cursor_->first <= end_ ? cursor_->first : (begin_ + end_) / 2;
        zoom(e == Event::Character('-') ? 1 / kWheelStep : kWheelStep, anchor);
        return true;
    }
    if (e == Event::Character('>') || e == Event::Character('.')) return pan(0.2 * (end_ - begin_)), true;
    if (e == Event::Character('<') || e == Event::Character(',')) return pan(-0.2 * (end_ - begin_)), true;
    if (e == Event::Character('0')) return zoom(1e-12, begin_), true;
    if (e == Event::ArrowLeft || e == Event::ArrowRight || e == Event::ArrowUp || e == Event::ArrowDown) {
        if (!valid(plot_box_)) return false;
        const int width = std::max(1, plot_box_.x_max - plot_box_.x_min + 1);
        auto c = cursor_.value_or(std::pair{(begin_ + end_) / 2, 1000.0});
        if (e == Event::ArrowLeft || e == Event::ArrowRight) c.first = std::clamp(c.first + (e == Event::ArrowLeft ? -1 : 1) * (end_ - begin_) / width, begin_, end_);
        else {
            const int height = std::max(1, plot_box_.y_max - plot_box_.y_min + 1);
            const double nyq = nyquist();
            const double step = log_view() ? std::pow(nyq / kFmin, 1.0 / height) : 0;
            c.second = log_view() ? (e == Event::ArrowUp ? c.second * step : c.second / step) : c.second + (e == Event::ArrowUp ? 1 : -1) * nyq / height;
            c.second = std::clamp(c.second, log_view() ? kFmin : 0.0, nyq);
        }
        cursor_ = c;
        return true;
    }
    return false;
}

ftxui::Box SpectrumScreen::area(std::string_view id) const {
    if (id == "spectrogram") return plot_box_;
    if (id == "selection_plot") return zoom_.box("selection");
    if (id == "spectrum_panel") return panel_boxes_[spectrum_panel];
    if (id == "cursor_panel") return panel_boxes_[cursor_panel];
    if (id == "measure_panel") return panel_boxes_[measure_panel];
    if (id == "list_panel") return panel_boxes_[list_panel];
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

std::vector<JumpTarget> SpectrumScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
}

std::optional<std::string> SpectrumScreen::explain_at(int x, int y) const {
    for (const auto& c : clickables_)
        if (valid(c.box) && c.box.Contain(x, y)) return notes_.explain(c.id);
    return notes_.at(x, y);
}

std::optional<std::pair<int, int>> SpectrumScreen::menu_anchor() const {
    const auto& b = panel_boxes_[static_cast<std::size_t>(focus_)];
    if (b.x_max < b.x_min) return std::nullopt;
    return std::pair{(b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2};
}

std::shared_ptr<TabScreen> make_spectrum_screen(UiContext& ui, std::shared_ptr<SpectrumModel> model) {
    return std::make_shared<SpectrumScreen>(ui, std::move(model));
}

}  // namespace signals::tui
