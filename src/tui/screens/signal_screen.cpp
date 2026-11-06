#include "tui/screens/signal_screen.hpp"
#include "tui/widgets/screen_util.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <fmt/format.h>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

using namespace std::chrono_literals;

constexpr double kWheelStep = 1.25;
constexpr auto kDoubleClick = 400ms;
constexpr int kMinLeft = 24;
constexpr int kSourceRows = 10;
constexpr int kLogRows = 8;
std::string channel_name(std::size_t c, std::size_t channels) {
    if (channels == 2) return c == 0 ? "L" : "R";
    if (channels == 1) return "";
    return std::to_string(c + 1);
}

std::string bits_text(const io::WavInfo& info) {
    using io::SampleFormat;
    switch (info.format) {
    case SampleFormat::pcm_u8: return fmt::format(fmt::runtime(tr("tui.signal.bits")), fmt::arg("bits", 8));
    case SampleFormat::pcm_s16: return fmt::format(fmt::runtime(tr("tui.signal.bits")), fmt::arg("bits", 16));
    case SampleFormat::pcm_s24: return fmt::format(fmt::runtime(tr("tui.signal.bits")), fmt::arg("bits", 24));
    case SampleFormat::pcm_s32: return fmt::format(fmt::runtime(tr("tui.signal.bits")), fmt::arg("bits", 32));
    case SampleFormat::float32: return fmt::format(fmt::runtime(tr("tui.signal.float")), fmt::arg("bits", 32));
    case SampleFormat::float64: return fmt::format(fmt::runtime(tr("tui.signal.float")), fmt::arg("bits", 64));
    case SampleFormat::alaw: return "A-law";
    case SampleFormat::mulaw: return "μ-law";
    }
    return {};
}

std::string format_text(const io::WavInfo& info) {
    const bool pcm = info.format != io::SampleFormat::float32 && info.format != io::SampleFormat::float64 &&
                     info.format != io::SampleFormat::alaw && info.format != io::SampleFormat::mulaw;
    return (pcm ? "PCM " : "") + bits_text(info) + (info.extensible ? " · EXTENSIBLE" : " · WAVE");
}


}  // namespace

SignalScreen::SignalScreen(UiContext& ui, std::shared_ptr<SignalModel> model)
    : ui_{ui}, model_{std::move(model)}, layout_{{{"left", {37, 34, 37, 40, 44}}}} {
    panel_boxes_.fill(kNoBox);
    title_boxes_.fill(kNoBox);
    wave_boxes_.fill(kNoBox);
    divider_box_ = kNoBox;
    drag_chip_ = kNoBox;
}

std::string SignalScreen::tab_label() const { return tr("tui.tab.signal"); }

std::vector<Hint> SignalScreen::hints() const {
    std::vector<Hint> out{{"o", tr("tui.signal.hint.open")},
                          {"Space", tr("tui.signal.hint.play")},
                          {tr("tui.signal.key.wheel"), tr("tui.signal.hint.wheel")},
                          {"s", tr("tui.signal.hint.drag_mode")},
                          {tr("tui.signal.key.click"), tr("tui.signal.hint.click")},
                          {tr("tui.signal.key.drag"), tr("tui.signal.hint.drag")},
                          {"z", tr("tui.signal.hint.zoom")},
                          {"Tab", tr("tui.signal.hint.tab")},
                          {"←→", tr("tui.signal.hint.arrows")},
                          {"b", tr("tui.signal.hint.cursor_b")},
                          {"Esc", tr("tui.signal.hint.cancel")},
                          {tr("tui.signal.key.ctrl_drag"), tr("tui.signal.hint.select")}};
    if (breakpoint_ <= Breakpoint::small) std::rotate(out.begin(), out.begin() + 7, out.begin() + 8);
    return out;
}

std::string SignalScreen::context() const {
    auto d = model_->data();
    if (!d) return {};
    return fmt::format(fmt::runtime(tr("tui.signal.context")), fmt::arg("name", d->path.filename().string()),
                       fmt::arg("channels", d->info.channels), fmt::arg("rate", format_si(d->info.sample_rate, "Гц")),
                       fmt::arg("bits", bits_text(d->info)));
}

std::string SignalScreen::key_of(Panel p) {
    static constexpr std::array<std::string_view, panel_count> keys{"wave", "spectrum", "bands", "source", "run", "log"};
    return std::string{keys[static_cast<std::size_t>(p)]};
}

SignalScreen::Panel SignalScreen::panel_of(const std::string& key) {
    for (int p = 0; p < panel_count; ++p)
        if (key_of(static_cast<Panel>(p)) == key) return static_cast<Panel>(p);
    return wave;
}

bool SignalScreen::loading() const {
    const auto id = model_->job();
    if (!id || !ui_.jobs) return false;
    for (const auto& v : ui_.jobs->snapshot())
        if (v.id == *id) return !v.finished;
    return false;
}

ftxui::Element SignalScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    if (breakpoint_ <= Breakpoint::small && !layout_.maximized())
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1),
                                     fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_, &title_boxes_[static_cast<std::size_t>(p)]) |
           ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element SignalScreen::source_panel(const SignalData* d) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!d) return frame_panel(source, vbox({text(tr("tui.signal.empty")) | fg(th.dim, ui_), text(tr("tui.signal.empty_hint")) | fg(th.dim, ui_)}), {},
                               tr("tui.signal.source"));
    auto kv = [&](const std::string& key, std::string value) {
        return hbox({text(pad_right(key, 9)) | fg(th.dim, ui_), text(std::move(value)) | fg(th.fg, ui_)});
    };
    const std::size_t n = d->stats.size();
    auto per_channel = [&](auto value) {
        std::string out;
        for (std::size_t c = 0; c < n; ++c) {
            const auto name = channel_name(c, n);
            out += (c ? "  " : "") + (name.empty() ? "" : name + " ") + value(d->stats[c]);
        }
        return out + " дБFS";
    };
    Elements rows{text(d->path.filename().string()) | bold | fg(th.fg, ui_), kv(tr("tui.signal.format"), format_text(d->info)),
                  kv(tr("tui.signal.channels"), n == 2 ? "2 (L, R)" : std::to_string(n)),
                  kv(tr("tui.signal.rate"), group(d->info.sample_rate) + " Гц"),
                  kv(tr("tui.signal.length"), fmt::format(fmt::runtime(tr("tui.signal.frames")), fmt::arg("count", group(d->info.frames)),
                                                          fmt::arg("time", format_fixed(d->duration(), 2) + " с"))),
                  kv(tr("tui.signal.peak"), per_channel([](const io::ChannelStats& s) { return format_fixed(s.peak_dbfs, 1); })),
                  kv(tr("tui.signal.rms"), per_channel([](const io::ChannelStats& s) { return format_fixed(s.rms_dbfs, 1); }))};
    if (d->full_scale_samples > 0) {
        std::string clip = std::string{glyph(Glyph::warn, ui_.caps.glyphs)} + " " + tr("tui.signal.clipping");
        for (std::size_t c = 0; c < n; ++c) {
            const auto name = channel_name(c, n);
            clip += (c ? " · " : " ") + (name.empty() ? "" : name + " ") + group(d->stats[c].clipped);
        }
        rows.push_back(text(clip) | fg(th.warn, ui_));
    } else {
        rows.push_back(text(tr("tui.signal.no_clipping")) | fg(th.dim, ui_));
    }
    return frame_panel(source, vbox(std::move(rows)), {}, tr("tui.signal.source"));
}

ftxui::Element SignalScreen::run_panel(int panel_width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    std::optional<JobView> job;
    if (ui_.jobs) {
        const auto id = model_->job();
        for (auto& v : ui_.jobs->snapshot())
            if (!id || v.id == *id) job = std::move(v);
    }
    if (!job) return frame_panel(progress, text(tr("tui.signal.no_job")) | fg(th.dim, ui_), {}, tr("tui.signal.run"));
    const int width = std::max(10, panel_width - 4);
    Elements rows;
    double elapsed = 0;
    bool running = false;
    const auto g = ui_.caps.glyphs;
    for (const auto& s : job->stages) {
        std::string icon;
        Rgb color = th.dim;
        switch (s.state) {
        case StageState::waiting: icon = glyph(Glyph::bullet_off, g); break;
        case StageState::running: icon = glyph(Glyph::bullet_on, g), color = th.accent, running = true; break;
        case StageState::done: icon = glyph(Glyph::check, g), color = th.ok; break;
        case StageState::failed: icon = glyph(Glyph::cross, g), color = th.err; break;
        case StageState::skipped: icon = "–"; break;
        }
        elapsed += s.seconds;
        const auto time = s.state == StageState::waiting ? std::string{"—"} : format_si(s.seconds, "с");
        rows.push_back(hbox({text(icon) | fg(color, ui_), text(" " + s.name) | fg(s.state == StageState::waiting ? th.dim : th.fg, ui_), filler(),
                             text(" " + time) | fg(th.dim, ui_)}));
        if (s.state == StageState::running)
            rows.push_back(hbox({text("  " + bar(s.fraction, std::max(4, width - 9), g)) | fg(th.accent, ui_),
                                 text(" " + pad_left(format_fixed(100 * s.fraction, 0) + " %", 5))}));
        if (!s.detail.empty()) rows.push_back(text("  " + s.detail) | fg(th.dim, ui_));
    }
    if (job->error) rows.push_back(text(*job->error) | fg(th.err, ui_));
    if (job->cancelled) rows.push_back(text(tr("tui.jobs.cancelled")) | fg(th.warn, ui_));
    rows.push_back(filler());
    std::string rule;
    for (int i = 0; i < width; ++i) rule += glyph(Glyph::h, g);
    rows.push_back(text(rule) | fg(th.dim, ui_));
    const auto threads = ui_.resources ? ui_.resources().threads : 0;
    rows.push_back(line({{tr("tui.signal.elapsed") + " ", th.dim}, {pad_left(format_si(elapsed, "с"), 8), th.fg},
                         {" · " + tr("tui.signal.threads") + " ", th.dim}, {std::to_string(threads), th.fg}},
                        ui_));
    if (running) rows.push_back(hbox({key_label(" Esc ", ui_), text(" " + tr("tui.signal.cancel")) | fg(th.dim, ui_)}));
    return frame_panel(progress, vbox(std::move(rows)), {{"#" + std::to_string(job->id), false}}, tr("tui.signal.run") + " · " + job->title);
}

ftxui::Element SignalScreen::log_panel(int rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto lines = model_->log();
    Elements out;
    const auto shown = static_cast<std::size_t>(std::max(1, rows - 2));
    for (std::size_t i = lines.size() > shown ? lines.size() - shown : 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        const Rgb c = l.level == LogLine::error ? th.err : l.level == LogLine::warn ? th.warn : th.fg;
        const std::string mark = l.level == LogLine::warn ? std::string{glyph(Glyph::warn, ui_.caps.glyphs)} + " " : "";
        out.push_back(hbox({text(l.time + " ") | fg(th.dim, ui_), text(mark + l.text) | fg(c, ui_)}));
    }
    if (out.empty()) out.push_back(text(tr("tui.signal.no_log")) | fg(th.dim, ui_));
    return frame_panel(log, vbox(std::move(out)), {}, tr("tui.signal.log"));
}

int SignalScreen::wave_columns() const {
    if (valid(wave_boxes_[0])) {
        PlotSpec s;
        s.labels = wave_labels_;
        s.x_labels = !wave_split_;
        const auto a = plot_area(wave_boxes_[0], s);
        return std::max(1, a.x_max - a.x_min + 1);
    }
    return 1;
}

ftxui::Element SignalScreen::wave_panel(const std::shared_ptr<const SignalData>& d, int rows, int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!d)
        return frame_panel(wave, centered({text(tr("tui.signal.empty")) | fg(th.dim, ui_), text(tr("tui.signal.empty_hint")) | fg(th.fg, ui_)}),
                           {}, tr("tui.signal.wave"));
    const double fs = d->info.sample_rate;
    const auto head_at = playhead();
    if (head_at && *head_at > model_->view_end()) {
        const double s = model_->view_end() - model_->view_begin();
        model_->pan(*head_at - (model_->view_begin() + 0.1 * s));
    }
    const double b = model_->view_begin(), e = model_->view_end(), span = e - b;
    const int plot_rows = std::max(2, rows - 4);
    const std::size_t shown = std::min<std::size_t>(2, d->samples.size());
    wave_split_ = shown == 2;
    const int l_rows = wave_split_ ? (plot_rows - 1) / 2 : plot_rows;
    const int r_rows = plot_rows - l_rows;
    wave_labels_ = l_rows >= 6 && (!wave_split_ || r_rows >= 6);
    const int columns = std::max(1, width - 4 - (wave_labels_ ? 6 : 0));
    const auto cursor_a = model_->cursor_a(), cursor_b = model_->cursor_b();
    const auto selection = model_->selection();
    Elements plots;
    for (std::size_t c = 0; c < shown; ++c) {
        PlotSpec spec;
        spec.x = {b, e};
        spec.y = {-model_->amplitude(), model_->amplitude()};
        spec.labels = wave_labels_;
        spec.x_labels = c + 1 == shown;
        const auto n = d->samples[c].size();
        const auto s0 = std::min(n, static_cast<std::size_t>(std::floor(b * fs)));
        const auto s1 = std::min(n, static_cast<std::size_t>(std::ceil(e * fs)));
        spec.envelopes.push_back(
            Envelope{{}, c == 0 ? th.fg : th.dim, [d, c, s0, s1](int cols) { return d->pyramids[c].query(s0, s1, static_cast<std::size_t>(cols)); }});
        spec.cursor_a = cursor_a;
        spec.cursor_b = cursor_b;
        spec.playhead = head_at;
        spec.selection = selection;
        auto chart = plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | reflect(wave_boxes_[c]);
        auto label = vbox({text(channel_name(c, d->samples.size())) | fg(th.dim, ui_)});
        plots.push_back(dbox({chart, label}) | size(HEIGHT, EQUAL, c == 0 ? l_rows : r_rows));
    }
    const double per_dot = span * fs / (2.0 * columns);
    const std::string ratio = per_dot >= 1 ? "1:" + group(static_cast<std::uint64_t>(std::llround(per_dot)))
                                           : group(static_cast<std::uint64_t>(std::llround(1 / std::max(per_dot, 1e-9)))) + ":1";
    std::vector<Span> head{{tr("tui.signal.cursor") + " A ", th.dim}};
    if (cursor_a) {
        const auto frame = static_cast<std::size_t>(std::min<std::uint64_t>(d->info.frames ? d->info.frames - 1 : 0, static_cast<std::uint64_t>(*cursor_a * fs)));
        head.push_back({time_text(*cursor_a, span), th.accent});
        head.push_back({" · " + tr("tui.signal.frame") + " ", th.dim});
        head.push_back({group(frame), th.fg});
        const auto half = static_cast<std::size_t>(std::max(1.0, per_dot) / 2);
        for (std::size_t c = 0; c < d->pyramids.size() && c < 2; ++c) {
            const auto n = d->pyramids[c].size();
            const auto lo = frame > half ? frame - half : 0;
            const auto hi = std::min<std::size_t>(n, frame + half + 1);
            const auto r = d->pyramids[c].query(lo, std::max(hi, lo + 1), 1);
            const auto name = channel_name(c, d->pyramids.size());
            head.push_back({"  " + (name.empty() ? "" : name + " "), th.dim});
            head.push_back({r.empty() ? "—" : "[" + format_fixed(r[0].lo, 2) + "; " + format_fixed(r[0].hi, 2) + "]", th.fg});
        }
    } else {
        head.push_back({"—", th.fg});
    }
    head.push_back({"   B ", th.dim});
    if (cursor_b) {
        head.push_back({time_text(*cursor_b, span), th.fg});
        if (cursor_a) head.push_back({"  Δ " + time_text(std::abs(*cursor_b - *cursor_a), span), th.dim});
    } else {
        head.push_back({"—", th.fg});
    }
    head.push_back({std::string{ui_.caps.glyphs == GlyphMode::ascii ? "   <-> " : "   ←→ "} + (arrows_move_b_ ? "B" : "A"), th.dim});
    std::vector<Span> sel_line;
    if (selection) {
        const auto [t0, t1] = *selection;
        sel_line = {{tr("tui.signal.selected") + " ", th.dim},
                    {format_fixed(t0, 2) + "–" + format_fixed(t1, 2) + " с", th.accent},
                    {" (" + time_text(t1 - t0, t1 - t0) + ")", th.dim}};
        const auto stats = model_->selection_stats();
        if (!stats.empty() && std::abs(stats[0].duration - (t1 - t0)) < 1e-9) {
            auto per = [&](double SelectionStats::*field) {
                std::string out;
                for (std::size_t c = 0; c < stats.size() && c < 2; ++c) {
                    const auto name = channel_name(c, stats.size());
                    out += (c ? "  " : "") + (name.empty() ? "" : name + " ") + format_fixed(stats[c].*field, 1);
                }
                return out;
            };
            sel_line.push_back({" · " + tr("tui.signal.peak") + " ", th.dim});
            sel_line.push_back({per(&SelectionStats::peak_dbfs), th.fg});
            sel_line.push_back({" · " + tr("tui.signal.rms") + " ", th.dim});
            sel_line.push_back({per(&SelectionStats::rms_dbfs) + " дБFS", th.fg});
        }
    } else {
        sel_line = {{tr("tui.signal.no_selection"), th.dim}};
    }
    Elements body{line(head, ui_), line(sel_line, ui_)};
    for (auto& p : plots) body.push_back(std::move(p));
    drag_chip_ = kNoBox;
    const bool selecting = drag_mode_ == DragMode::select;
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::string play_chip = head_at ? std::string{ascii ? "> " : "▶ "} + format_fixed(*head_at, 2) + " с" : std::string{ascii ? "#" : "■"};
    std::vector<Chip> chips{{tr(selecting ? "tui.signal.chip.drag_select" : "tui.signal.chip.drag_pan"), selecting, &drag_chip_},
                            {tr("tui.signal.chip.minmax"), true},
                            {fmt::format(fmt::runtime(tr("tui.signal.chip.scale")), fmt::arg("ratio", ratio)), false}};
    if (ui_.player) chips.insert(chips.begin(), Chip{play_chip, head_at.has_value()});
    return frame_panel(wave, vbox(std::move(body)), std::move(chips), tr("tui.signal.wave"));
}

ftxui::Element SignalScreen::spectrum_panel(const SignalData* d, int, int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    std::vector<Chip> chips{{tr("tui.signal.chip.logf"), true}, {tr("tui.signal.chip.floor"), false}, {tr("tui.signal.chip.edges"), false}};
    if (!d || d->psd_db.empty())
        return frame_panel(spectrum, centered({text(d ? tr("tui.signal.no_spectrum") : "—") | fg(th.dim, ui_)}), std::move(chips),
                           tr("tui.signal.spectrum"));
    const double fs = d->info.sample_rate;
    PlotSpec spec;
    spec.x = {20, fs / 2, AxisScale::log10};
    spec.y = {-100, 0};
    Trace t;
    t.color = th.trace[0];
    for (std::size_t k = 0; k < d->psd_hz.size(); ++k)
        if (d->psd_hz[k] >= 20) {
            t.x.push_back(d->psd_hz[k]);
            t.y.push_back(d->psd_db[k]);
        }
    spec.traces.push_back(std::move(t));
    const std::size_t levels = d->band_energy.empty() ? 0 : d->band_energy.size() - 1;
    for (std::size_t k = 2; k <= levels + 1; ++k) spec.x_marks.push_back(fs / std::ldexp(1.0, static_cast<int>(k)));
    const int columns = std::max(1, width - 4 - 6);
    Elements marks;
    int pos = 0;
    for (std::size_t k = 0; levels > 0 && k <= levels; ++k) {
        const auto [lo, hi] = band_range(k, levels, fs);
        const auto name = band_name(k, levels, ui_.caps.glyphs);
        const int col = 6 + column_of_value(spec.x, std::sqrt(std::max(lo, 20.0) * hi), columns) - display_width(name) / 2;
        if (col < pos) continue;
        marks.push_back(text(std::string(static_cast<std::size_t>(col - pos), ' ')));
        marks.push_back(text(name) | fg(th.trace[0], ui_));
        pos = col + display_width(name);
    }
    return frame_panel(spectrum, vbox({hbox(std::move(marks)), plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | flex}), std::move(chips),
                       tr("tui.signal.spectrum"));
}

ftxui::Element SignalScreen::bands_panel(const SignalData* d, int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const std::size_t levels = d && !d->band_energy.empty() ? d->band_energy.size() - 1 : 6;
    const auto title = fmt::format(fmt::runtime(tr("tui.signal.bands")), fmt::arg("levels", levels));
    if (!d || d->band_energy.empty())
        return frame_panel(bands, text(d ? tr("tui.signal.no_bands") : "—") | fg(th.dim, ui_), {}, title);
    const double fs = d->info.sample_rate;
    const int bar_width = std::clamp(width - 4 - 4 - 17 - 9 - 9, 8, 40);
    Elements rows;
    for (std::size_t k = 0; k < d->band_energy.size(); ++k) {
        const auto [lo, hi] = band_range(k, levels, fs);
        const double f = d->band_energy[k];
        rows.push_back(hbox({text(pad_right(band_name(k, levels, ui_.caps.glyphs), 3)) | bold | fg(th.trace[0], ui_),
                             text(" " + pad_right(hz_text(lo) + "–" + hz_text(hi) + " Гц", 16)) | fg(th.dim, ui_),
                             text(" " + bar(f, bar_width, ui_.caps.glyphs)) | fg(th.trace[0], ui_),
                             text(pad_left(format_fixed(100 * f, 2) + " %", 9)) | fg(th.fg, ui_),
                             text("  " + (k < d->band_lengths.size() ? group(d->band_lengths[k]) : std::string{"—"})) | fg(th.dim, ui_)}));
    }
    return frame_panel(bands, vbox(std::move(rows)), {}, title);
}

ftxui::Element SignalScreen::build(Panel p, const std::shared_ptr<const SignalData>& d, int rows, int width) {
    switch (p) {
    case wave: return wave_panel(d, rows, width);
    case spectrum: return spectrum_panel(d.get(), rows, width);
    case bands: return bands_panel(d.get(), width);
    case source: return source_panel(d.get());
    case progress: return run_panel(width);
    case log: return log_panel(rows);
    case panel_count: break;
    }
    return ftxui::text("");
}

ftxui::Element SignalScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    title_boxes_.fill(kNoBox);
    wave_boxes_.fill(kNoBox);
    divider_box_ = kNoBox;
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const int height = std::max(3, rows - 3);
    const auto d = model_->data();
    if (breakpoint_ <= Breakpoint::small || layout_.maximized()) {
        const Panel p = layout_.maximized() ? panel_of(*layout_.maximized()) : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        return build(p, d, height, cols) | reflect(root_box_);
    }
    int& left = layout_.size("left");
    left = std::clamp(left, kMinLeft, std::max(kMinLeft, cols / 2));
    const int right = cols - left - 1;
    const int band_rows = d && !d->band_energy.empty() ? static_cast<int>(d->band_energy.size()) + 2 : 9;
    int gaps = 2;
    int remaining = height - band_rows - gaps;
    int wave_rows = std::max(17, remaining * 17 / 30);
    int spectrum_rows = remaining - wave_rows;
    if (spectrum_rows < 9) {
        gaps = 0;
        remaining = height - band_rows;
        wave_rows = std::max(12, remaining * 17 / 30);
        spectrum_rows = std::max(6, remaining - wave_rows);
    }
    Elements right_column{build(wave, d, wave_rows, right) | size(HEIGHT, EQUAL, wave_rows)};
    if (gaps) right_column.push_back(text(""));
    right_column.push_back(build(spectrum, d, spectrum_rows, right) | size(HEIGHT, EQUAL, spectrum_rows));
    if (gaps) right_column.push_back(text(""));
    right_column.push_back(build(bands, d, band_rows, right) | flex);
    auto left_column = vbox({build(source, d, kSourceRows, left) | size(HEIGHT, EQUAL, kSourceRows), text(""), build(progress, d, 0, left) | flex, text(""),
                             build(log, d, kLogRows, left) | size(HEIGHT, EQUAL, kLogRows)});
    return hbox({left_column | size(WIDTH, EQUAL, left), filler() | size(WIDTH, EQUAL, 1) | reflect(divider_box_),
                 vbox(std::move(right_column)) | flex}) |
           reflect(root_box_);
}

bool SignalScreen::in_wave(int x, int y) const {
    for (std::size_t c = 0; c < wave_boxes_.size(); ++c) {
        if (!valid(wave_boxes_[c])) continue;
        PlotSpec s;
        s.labels = wave_labels_;
        s.x_labels = c == 1 || !wave_split_;
        if (plot_area(wave_boxes_[c], s).Contain(x, y)) return true;
    }
    return false;
}

double SignalScreen::time_at(int x) const {
    PlotSpec s;
    s.labels = wave_labels_;
    s.x_labels = !wave_split_;
    const auto a = plot_area(wave_boxes_[0], s);
    return value_at_column(Axis{model_->view_begin(), model_->view_end()}, x - a.x_min, a.x_max - a.x_min + 1);
}

ftxui::Box SignalScreen::area(std::string_view id) const {
    if (id == "drag_chip") return drag_chip_;
    if (id == "waveform") {
        ftxui::Box out = kNoBox;
        bool any = false;
        for (std::size_t c = 0; c < wave_boxes_.size(); ++c) {
            if (!valid(wave_boxes_[c])) continue;
            PlotSpec s;
            s.labels = wave_labels_;
            s.x_labels = c == 1 || !wave_split_;
            const auto a = plot_area(wave_boxes_[c], s);
            out = any ? ftxui::Box::Union(out, a) : a;
            any = true;
        }
        return out;
    }
    return panel_boxes_[static_cast<std::size_t>(panel_of(std::string{id}))];
}

bool SignalScreen::on_mouse(const ftxui::Mouse& m) {
    using ftxui::Mouse;
    if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
        if (!model_->data() || !in_wave(m.x, m.y)) return false;
        const double f = m.button == Mouse::WheelUp ? kWheelStep : 1 / kWheelStep;
        if (m.shift) model_->zoom_amplitude(f);
        else model_->zoom(f, time_at(m.x));
        return true;
    }
    if (m.button == Mouse::WheelLeft || m.button == Mouse::WheelRight) {
        if (!model_->data() || !in_wave(m.x, m.y)) return false;
        const double span = model_->view_end() - model_->view_begin();
        model_->pan((m.button == Mouse::WheelRight ? 0.1 : -0.1) * span);
        return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        if (drag_chip_.Contain(m.x, m.y)) {
            drag_mode_ = drag_mode_ == DragMode::pan ? DragMode::select : DragMode::pan;
            return true;
        }
        if (divider_box_.Contain(m.x, m.y)) {
            drag_ = {Drag::divider, m.x};
            return true;
        }
        for (int p = 0; p < panel_count; ++p) {
            if (!title_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) continue;
            const auto now = std::chrono::steady_clock::now();
            if (last_title_ == p && now - last_title_click_ < kDoubleClick) {
                layout_.toggle_maximize(key_of(static_cast<Panel>(p)));
                last_title_ = panel_count;
            } else {
                last_title_ = static_cast<Panel>(p);
                last_title_click_ = now;
            }
            focus_ = static_cast<Panel>(p);
            return true;
        }
        bool hit = false;
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) focus_ = static_cast<Panel>(p), hit = true;
        if (model_->data() && in_wave(m.x, m.y)) {
            const bool select = m.control || drag_mode_ == DragMode::select;
            drag_ = {select ? Drag::select : Drag::pan, m.x, time_at(m.x), false, m.shift};
            return true;
        }
        return hit;
    }
    if (drag_.kind == Drag::none) return false;
    if (m.motion == Mouse::Released) {
        if (drag_.kind == Drag::pan || drag_.kind == Drag::select) {
            if (!drag_.moved) {
                if (drag_.shift) model_->set_cursor_b(time_at(m.x));
                else model_->set_cursor_a(time_at(m.x));
            } else if (drag_.kind == Drag::select) {
                model_->select(drag_.t0, time_at(m.x));
                if (ui_.jobs) model_->compute_selection(*ui_.jobs);
            }
        }
        drag_.kind = Drag::none;
        return true;
    }
    if (m.x != drag_.last_x) drag_.moved = true;
    switch (drag_.kind) {
    case Drag::pan: {
        const double span = model_->view_end() - model_->view_begin();
        model_->pan((drag_.last_x - m.x) * span / wave_columns());
        break;
    }
    case Drag::select: model_->select(drag_.t0, time_at(m.x)); break;
    case Drag::divider: {
        const int cols = root_box_.x_max - root_box_.x_min + 1;
        layout_.size("left") = std::clamp(m.x - root_box_.x_min, kMinLeft, std::max(kMinLeft, cols / 2));
        break;
    }
    case Drag::none: break;
    }
    drag_.last_x = m.x;
    return true;
}

bool SignalScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (e.is_mouse()) return on_mouse(e.mouse());
    if (e == Event::Character(' ')) {
        toggle_playback();
        return true;
    }
    if (e == Event::Escape) {
        if (playing()) {
            ui_.player->stop();
            return true;
        }
        if (loading()) {
            ui_.jobs->cancel(*model_->job());
            return true;
        }
        if (layout_.maximized()) {
            layout_.toggle_maximize(*layout_.maximized());
            return true;
        }
        return false;
    }
    if (e == Event::Character('o')) {
        if (ui_.ask_open) ui_.ask_open();
        return true;
    }
    if (e == Event::Character('z')) {
        layout_.toggle_maximize(key_of(focus_));
        return true;
    }
    if (e == Event::Character('s')) {
        drag_mode_ = drag_mode_ == DragMode::pan ? DragMode::select : DragMode::pan;
        return true;
    }
    if (e == Event::Tab) {
        if (breakpoint_ <= Breakpoint::small && !layout_.maximized()) layout_.next_panel(panel_count);
        else focus_ = static_cast<Panel>((focus_ + 1) % panel_count);
        return true;
    }
    const auto d = model_->data();
    if (!d) return false;
    const double begin = model_->view_begin(), end = model_->view_end();
    const double step = (end - begin) / (2.0 * wave_columns());
    auto move_cursor = [&](double t) {
        if (arrows_move_b_) model_->set_cursor_b(t);
        else model_->set_cursor_a(t);
        const double a = arrows_move_b_ ? *model_->cursor_b() : *model_->cursor_a();
        if (a < model_->view_begin()) model_->pan(a - model_->view_begin());
        if (a > model_->view_end()) model_->pan(a - model_->view_end());
        return true;
    };
    if (e == Event::Character('b')) {
        arrows_move_b_ = !arrows_move_b_;
        if (arrows_move_b_ && !model_->cursor_b()) model_->set_cursor_b(model_->cursor_a().value_or((begin + end) / 2));
        return true;
    }
    const double current = (arrows_move_b_ ? model_->cursor_b() : model_->cursor_a()).value_or((begin + end) / 2);
    if (e == Event::ArrowLeft) return move_cursor(current - step);
    if (e == Event::ArrowRight) return move_cursor(current + step);
    if (e == Event::Home) return move_cursor(0);
    if (e == Event::End) return move_cursor(d->duration());
    return false;
}

bool SignalScreen::playing() const {
    return ui_.player && ui_.player->state() == audio::PlayState::playing && ui_.player->tag() == "signal";
}

std::optional<double> SignalScreen::playhead() const {
    const auto d = model_->data();
    if (!playing() || !d || d->info.sample_rate == 0) return std::nullopt;
    return static_cast<double>(ui_.player->position()) / d->info.sample_rate;
}

void SignalScreen::toggle_playback() {
    if (!ui_.player) return;
    if (playing()) {
        ui_.player->stop();
        return;
    }
    const auto d = model_->data();
    auto clip = model_->clip();
    if (!d || !clip) return;
    const double fs = d->info.sample_rate;
    double t0 = model_->cursor_a().value_or(model_->view_begin());
    std::optional<std::size_t> to;
    if (const auto s = model_->selection(); s && s->second > s->first) {
        t0 = s->first;
        to = static_cast<std::size_t>(std::llround(s->second * fs));
    }
    const auto from = static_cast<std::size_t>(std::llround(t0 * fs));
    if (auto r = ui_.player->play(std::move(*clip), from, to); !r && ui_.player->state() == audio::PlayState::unavailable && !audio_warned_) {
        audio_warned_ = true;
        model_->add_log(fmt::format(fmt::runtime(tr("tui.signal.audio_unavailable")), fmt::arg("reason", r.error().message)), LogLine::warn);
    }
}

std::vector<MenuItem> SignalScreen::context_menu(int x, int y) {
    if (!model_->data() || !in_wave(x, y)) return {};
    const double t = time_at(x);
    const bool both = model_->cursor_a() && model_->cursor_b();
    return {
        {tr("tui.menu.cursor_a"), tr("tui.signal.key.click"), [this, t] { model_->set_cursor_a(t); }},
        {tr("tui.menu.cursor_b"), "Shift+" + tr("tui.signal.key.click"), [this, t] { model_->set_cursor_b(t); }},
        {tr("tui.menu.select_ab"), "", [this] {
             model_->select(*model_->cursor_a(), *model_->cursor_b());
             if (ui_.jobs) model_->compute_selection(*ui_.jobs);
         }, both},
        {tr("tui.menu.clear_selection"), "", [this] { model_->clear_selection(); }, model_->selection().has_value()},
        {tr("tui.menu.whole_signal"), "", [this] {
             model_->zoom(1e-12, 0);
             model_->zoom_amplitude(1e-12);
         }},
    };
}

std::vector<JumpTarget> SignalScreen::jump_targets() const {
    std::vector<JumpTarget> out{{drag_chip_, tr("tui.signal.chip.drag_pan")}};
    for (int p = 0; p < panel_count; ++p) out.push_back({title_boxes_[static_cast<std::size_t>(p)], key_of(static_cast<Panel>(p))});
    return out;
}

std::shared_ptr<TabScreen> make_signal_screen(UiContext& ui, std::shared_ptr<SignalModel> model) {
    return std::make_shared<SignalScreen>(ui, std::move(model));
}

}  // namespace signals::tui
