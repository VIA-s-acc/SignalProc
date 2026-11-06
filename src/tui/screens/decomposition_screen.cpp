#include "tui/screens/decomposition_screen.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/design/families.hpp"
#include "i18n/i18n.hpp"
#include "core/check.hpp"
#include "core/wavedec.hpp"
#include "core/wavelet/regularity.hpp"
#include "core/bank.hpp"
#include "tui/screens/publish_util.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/heatmap.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/list_window.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kLabelWidth = 27;
constexpr int kRightWidth = 39;
constexpr double kWheelStep = 1.25;
constexpr std::array<double, 6> kKeep{0, 0.5, 0.25, 0.1, 0.05, 0.01};
constexpr std::array<core::Extension, 4> kModes{core::Extension::symmetric, core::Extension::periodization, core::Extension::zero,
                                                core::Extension::reflect};

std::string pct(double v) { return format_fixed(v, 2) + " %"; }

}  // namespace

DecompositionScreen::DecompositionScreen(UiContext& ui, std::shared_ptr<DecompositionModel> model) : ui_{ui}, model_{std::move(model)} {
    panel_boxes_.fill(kNoBox);
    draft_.rule = core::ShrinkRule::bayes;
}

std::string DecompositionScreen::tab_label() const { return tr("tui.tab.decomposition"); }

std::vector<Hint> DecompositionScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},          {"z", tr("tui.signal.hint.zoom")},
            {"↑↓ PgUp PgDn", tr("tui.decomposition.hint.band")}, {"Enter", tr("tui.decomposition.hint.toggle")},
            {"←→ Home End", tr("tui.decomposition.hint.cursor")}, {"+ − 0 < >", tr("tui.hint.plot_zoom")},
            {"j/J", tr("tui.decomposition.hint.levels")},  {"e/E", tr("tui.decomposition.hint.edges")},
            {"w", tr("tui.decomposition.hint.wavelet")},
            {"Space", tr("tui.decomposition.hint.listen")}, {"a/b/x", tr("tui.decomposition.hint.ab")},
            {"v", tr("tui.decomposition.hint.view")},     {"u", tr("tui.decomposition.hint.undo")},
            {"F1", tr("tui.decomposition.hint.f1")},      {tr("tui.signal.key.wheel"), tr("tui.decomposition.hint.zoom")}};
}

std::optional<TheoryLink> DecompositionScreen::theory_link() const {
    return TheoryLink{TheoryLink::Kind::card, focus_ == denoise ? "threshold" : "dwt"};
}

void DecompositionScreen::publish(theory::LiveValues& v) const {
    const auto d = model_->data();
    const auto r = model_->recon();
    const auto s = model_->source();
    v.source = tab_label() + " ▸ " + (s ? s->path.filename().string() : std::string{"—"});
    if (!d) return;
    const auto& o = d->options;
    v.source += " · " + o.wavelet + " · J = " + std::to_string(o.levels);
    const auto bank = core::wavelet(o.wavelet);
    if (!bank) return;
    const auto n = d->x.size();
    const auto length = bank->length();
    const auto J = o.levels;
    v.set("block.n", group(n));
    v.set("block.wavelet", o.wavelet);
    v.set("block.levels", std::to_string(J));
    v.set("block.mode", std::string{core::to_string(o.mode)});
    v.set("block.filter_length", std::to_string(length));
    v.set("block.max_level", std::to_string(core::dwt_max_level(n, length)));
    std::vector<std::string> lengths, ranges, energies, names;
    std::size_t total = 0;
    for (std::size_t i = 0; i < d->coeffs.bands.size(); ++i) {
        const auto name = (i == 0 ? "a" : "d") + subscript_number(i == 0 ? J : J + 1 - i);
        names.push_back(name);
        lengths.push_back(name + " " + group(d->coeffs.bands[i].size()));
        total += d->coeffs.bands[i].size();
    }
    auto subscripted = [](const std::string& name) {
        std::string out;
        for (char ch : name) out += ch >= '0' && ch <= '9' ? subscript_number(static_cast<std::size_t>(ch - '0')) : std::string(1, ch);
        return out;
    };
    for (const auto& b : d->bands) {
        ranges.push_back(subscripted(b.name) + " " + hz_text(b.lo_hz) + "–" + hz_text(b.hi_hz));
        energies.push_back(subscripted(b.name) + " " + format_fixed(b.energy_pct, 2) + " %");
    }
    v.set("block.band_lengths", joined(lengths));
    v.set("block.band_sum", group(total) + (total > n ? " = N + " + std::to_string(total - n) : " = N"));
    v.set("block.band_ranges", joined(ranges));
    v.set("block.linear_phase", yes_no(core::is_linear_phase(bank->h0, 1e-9)));
    v.set("metric.multiplies", format_number(static_cast<double>(n) * static_cast<double>(bank->h0.coeffs.size() + bank->h1.coeffs.size()), 2));
    double peak = 0;
    for (double x : d->x) peak = std::max(peak, std::abs(x));
    v.set_check({"check.round_trip", o.wavelet, d->pr_error, core::default_tolerance(J, length, peak)});
    if (d->orthogonal) v.set_check(renamed(core::check_orthogonality(*bank, 1e-9), "check.orthogonality"));
    else v.set("check.orthogonality", tr("tui.theory.value.biorthogonal"));
    v.set_check(core::check_vanishing_moments(bank->h1, std::max(1, core::zeros_at_minus_one(bank->f0, 1e-6)), 1e-9));
    const bool exact = d->orthogonal && o.mode == core::Extension::periodization && n % (std::size_t{1} << J) == 0;
    if (exact) v.set_check({"check.parseval", o.wavelet, d->parseval_error, 1e-10});
    else
        v.set("check.parseval", fmt::format(fmt::runtime(tr("tui.theory.value.parseval_approx")), fmt::arg("value", format_number(d->parseval_error, 2))));
    v.set("block.bands", joined(names));
    v.set("metric.energies", joined(energies));
    const auto& p = r ? r->processing : Processing{};
    v.set("block.rule", p.rule ? std::string{*p.rule == core::ShrinkRule::bayes ? "BayesShrink" : "VisuShrink"} : tr("tui.theory.value.none"));
    v.set("block.mode", std::string{core::to_string(o.mode)});
    v.set("block.levels", std::to_string(J));
    std::vector<std::string> lambdas;
    if (r)
        for (double l : r->lambdas) lambdas.push_back(format_number(l, 3));
    v.set("block.thresholds", lambdas.empty() ? std::string{"—"} : joined(lambdas));
    v.set("metric.sigma", format_number(d->sigma, 3));
    v.set("metric.kept", p.keep_fraction ? format_fixed(100 * *p.keep_fraction, 1) + " %" : std::string{"100 %"});
    const double kept = r ? r->energy_kept_pct : 100.0;
    v.set("metric.energy_removed", format_fixed(std::max(0.0, 100.0 - kept), 2) + " %");
    v.set("metric.energy_kept", format_fixed(kept, 2) + " %");
    std::vector<std::string> on, gains;
    bool any_off = false;
    const auto enabled = model_->enabled();
    for (std::size_t i = 0; i < enabled.size() && i < names.size(); ++i) {
        if (enabled[i]) on.push_back(names[names.size() - 1 - i]);
        any_off = any_off || !enabled[i];
    }
    for (std::size_t i = 0; i < p.gains_db.size() && i < names.size(); ++i)
        if (p.gains_db[i] != 0) gains.push_back(names[names.size() - 1 - i] + " " + format_fixed(p.gains_db[i], 1) + " " + tr("tui.unit.db"));
    v.set("block.enabled", on.empty() ? std::string{"—"} : joined(on));
    v.set("block.gains", gains.empty() ? "0 " + tr("tui.unit.db") : joined(gains));
    v.set("block.aliasing", tr(any_off ? "tui.theory.value.aliasing" : "tui.theory.value.no"));
}

std::string DecompositionScreen::context() const {
    const auto d = model_->data();
    const auto s = model_->source();
    if (!d || !s) return {};
    const auto o = d->options;
    const auto channel = o.channel < 0 ? tr("tui.decomposition.mono") : fmt::format(fmt::runtime(tr("tui.decomposition.channel")), fmt::arg("n", o.channel + 1));
    return fmt::format("{} · {} · {} · J = {} · {}", s->path.filename().string(), channel, o.wavelet, o.levels, core::to_string(o.mode));
}

std::string DecompositionScreen::band_label(std::size_t i) const {
    const auto d = model_->data();
    if (!d || i >= d->bands.size()) return {};
    const auto J = d->options.levels;
    return band_name(J - i, J, ui_.caps.glyphs);
}

void DecompositionScreen::set_cursor(double t) {
    const auto d = model_->data();
    cursor_ = std::clamp(t, 0.0, d ? d->duration() : 0.0);
}

void DecompositionScreen::sync_view(const std::shared_ptr<const DecompositionData>& d) {
    if (d == seen_) return;
    const bool same_signal = seen_ && d && seen_->x.size() == d->x.size();
    seen_ = d;
    if (!d) return;
    if (!same_signal) {
        begin_ = 0;
        end_ = d->duration();
        cursor_.reset();
    }
    selected_ = std::min(selected_, d->bands.size() - 1);
    draft_.last_level = std::min<std::size_t>(std::max<std::size_t>(draft_.last_level, 1), d->options.levels);
    draft_.gains_db.assign(d->bands.size(), 0.0);
}

void DecompositionScreen::zoom(double factor, double anchor) {
    const auto d = model_->data();
    if (!d) return;
    const double total = d->duration();
    const double min_span = std::min(total, 64.0 / std::max(1.0, d->fs));
    const double old = end_ - begin_;
    const double span = std::clamp(old / factor, min_span, total);
    anchor = std::clamp(anchor, begin_, end_);
    double b = anchor - (anchor - begin_) * (span / old);
    b = std::clamp(b, 0.0, total - span);
    begin_ = b;
    end_ = b + span;
}

void DecompositionScreen::pan(double dt) {
    const auto d = model_->data();
    if (!d) return;
    const double span = end_ - begin_;
    begin_ = std::clamp(begin_ + dt, 0.0, std::max(0.0, d->duration() - span));
    end_ = begin_ + span;
}

ftxui::Box DecompositionScreen::plot_area() const {
    ftxui::Box out = kNoBox;
    for (const auto& b : plot_boxes_)
        if (valid(b)) out = valid(out) ? ftxui::Box::Union(out, b) : b;
    return out;
}

double DecompositionScreen::time_at(int x) const {
    const auto plot_box_ = plot_area();
    const int width = std::max(1, plot_box_.x_max - plot_box_.x_min + 1);
    return begin_ + (static_cast<double>(x - plot_box_.x_min) + 0.5) / width * (end_ - begin_);
}

std::optional<double> DecompositionScreen::playhead() const {
    const auto d = model_->data();
    if (!ui_.player || !d || d->fs <= 0 || ui_.player->state() != audio::PlayState::playing || !ui_.player->tag().starts_with("decomposition"))
        return std::nullopt;
    return static_cast<double>(ui_.player->position()) / d->fs;
}

void DecompositionScreen::play(ClipKind kind) {
    if (!ui_.player) return;
    auto clip = model_->clip(kind, selected_);
    if (!clip) return;
    const auto d = model_->data();
    if (playhead()) {
        ui_.player->replace(std::move(*clip));
        return;
    }
    const auto from = static_cast<std::size_t>(std::llround(cursor_.value_or(begin_) * (d ? d->fs : 0.0)));
    if (auto r = ui_.player->play(std::move(*clip), from); !r) notice_ = fmt::format(fmt::runtime(tr("tui.signal.audio_unavailable")), fmt::arg("reason", r.error().message));
}

ftxui::Element DecompositionScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1),
                                     fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element DecompositionScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), kNoBox, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element DecompositionScreen::more_row(bool up, std::size_t count) const {
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::string arrow = ascii ? (up ? "^ " : "v ") : (up ? "▲ " : "▼ ");
    return ftxui::text(arrow + fmt::format(fmt::runtime(tr("tui.theory.blocks_more")), fmt::arg("count", count))) | fg(ui_.theme.dim, ui_);
}

ftxui::Element DecompositionScreen::toolbar(bool two_rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto chip = [&](std::string id, std::string label, bool on, bool later, std::function<void()> action) {
        auto e = text(on ? "[" + label + "]" : " " + label + " ") | fg(later ? th.dim : on ? th.accent : th.fg, ui_);
        if (on) e = e | bold;
        return clickable(std::move(id), e, std::move(action));
    };
    auto later = [this] { notice_ = tr("tui.decomposition.later"); };
    const auto o = model_->options();
    auto& jobs = *ui_.jobs;
    Elements parts{text(tr("tui.decomposition.view") + " ") | fg(th.dim, ui_),
                   chip("view_subbands", tr("tui.decomposition.view.subbands"), view_ == View::subbands, false, [this] { view_ = View::subbands; }),
                   chip("view_mra", "MRA", view_ == View::mra, false, [this] { view_ = View::mra; }),
                   chip("view_tiles", tr("tui.decomposition.view.tiles"), view_ == View::tiles, false, [this] { view_ = View::tiles; }),
                   chip("chip_packets", tr("tui.decomposition.view.packets"), false, true, later),
                   chip("chip_scalogram", tr("tui.decomposition.view.scalogram"), false, true, later)};
    Elements second{text((two_rows ? "" : " │ ") + tr("tui.decomposition.transform") + " ") | fg(th.dim, ui_),
                    chip("chip_dwt", "DWT", true, false, [] {}),
                    chip("chip_swt", "SWT", false, true, later),
                    chip("chip_modwt", "MODWT", false, true, later),
                    text(" │ " + tr("tui.decomposition.wavelet") + " ") | fg(th.dim, ui_),
                   clickable("wavelet", text(o.wavelet + " ▾") | fg(th.fg, ui_), [this] {
                       if (ui_.open_palette) ui_.open_palette(tr("tui.decomposition.cmd.wavelet"));
                   }),
                   text(" │ J ") | fg(th.dim, ui_),
                   clickable("levels_less", text("◂ ") | fg(th.fg, ui_), [this, &jobs] {
                       auto k = model_->options();
                       if (k.levels > 1) --k.levels;
                       model_->set_options(k, jobs);
                   }),
                   text(std::to_string(o.levels) + " ") | bold | fg(th.fg, ui_),
                   clickable("levels_more", text("▸") | fg(th.fg, ui_), [this, &jobs] {
                       auto k = model_->options();
                       ++k.levels;
                       model_->set_options(k, jobs);
                   }),
                   text(" │ " + tr("tui.decomposition.edges") + " ") | fg(th.dim, ui_),
                   clickable("edges", text(std::string{core::to_string(o.mode)} + " ▾") | fg(th.fg, ui_), [this, &jobs] {
                       auto k = model_->options();
                       auto it = std::find(kModes.begin(), kModes.end(), k.mode);
                       k.mode = it == kModes.end() || it + 1 == kModes.end() ? kModes.front() : *(it + 1);
                       model_->set_options(k, jobs);
                   })};
    if (two_rows) return vbox({hbox(std::move(parts)), hbox(std::move(second))});
    for (auto& e : second) parts.push_back(std::move(e));
    return hbox(std::move(parts));
}

ftxui::Element DecompositionScreen::time_axis(int width) const {
    std::string line(static_cast<std::size_t>(std::max(0, width)), ' ');
    std::vector<std::string> cells(static_cast<std::size_t>(std::max(0, width)), " ");
    const auto ticks = nice_ticks(begin_, end_, std::max(2, width / 14));
    int next_free = 0;
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        const auto label = ticks[i].label + (i + 1 == ticks.size() ? " с" : "");
        const auto cps = split_codepoints(label);
        int at = static_cast<int>(std::lround((ticks[i].value - begin_) / std::max(1e-12, end_ - begin_) * (width - 1)));
        at = std::clamp(at - static_cast<int>(cps.size()) / 2, 0, std::max(0, width - static_cast<int>(cps.size())));
        if (at < next_free) continue;
        for (std::size_t k = 0; k < cps.size() && at + static_cast<int>(k) < width; ++k) cells[static_cast<std::size_t>(at) + k] = cps[k];
        next_free = at + static_cast<int>(cps.size()) + 1;
    }
    std::string out;
    for (const auto& c : cells) out += c;
    return ftxui::text(out) | fg(ui_.theme.dim, ui_);
}

ftxui::Element DecompositionScreen::view_body(const DecompositionData& d, const Reconstruction* r, int width, int max_rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto J = d.options.levels;
    const int plot_width = std::max(10, width - kLabelWidth - 4);
    const auto head = playhead();
    plot_boxes_.clear();
    const auto bands = d.bands.size();
    plot_boxes_.resize(bands + 1, kNoBox);
    label_boxes_.assign(bands + 3, kNoBox);
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::string on_box = ascii ? "[x]" : "☑", off_box = ascii ? "[ ]" : "☐";
    auto colour = [&](std::size_t i) { return tint(th.trace[0], th.fg, 0.6 * (1.0 - static_cast<double>(i) / std::max<double>(1, static_cast<double>(J)))); };
    auto samples = [&](std::size_t len) {
        const double total = d.duration();
        const auto a = static_cast<std::size_t>(std::floor(begin_ / std::max(1e-12, total) * static_cast<double>(len)));
        auto b = static_cast<std::size_t>(std::ceil(end_ / std::max(1e-12, total) * static_cast<double>(len)));
        b = std::clamp(b, a + 1, len);
        return std::pair{std::min(a, len - 1), b};
    };
    Elements rows;
    const auto [first, shown] = list_window(band_first_, bands, std::max(3, max_rows - 1), 2, follow_ ? std::optional{selected_} : std::nullopt);
    const auto label_pad = [&](Element e) { return hbox({std::move(e) | size(WIDTH, EQUAL, kLabelWidth)}); };
    if (first > 0) rows.push_back(label_pad(more_row(true, first)) | reflect(label_boxes_[bands + 1]));
    if (view_ == View::tiles) {
        HeatmapSpec spec;
        spec.rows = std::min(shown, d.tiles.rows - std::min(first, d.tiles.rows));
        const auto c0 = static_cast<std::size_t>(std::floor(begin_ / std::max(1e-12, d.duration()) * static_cast<double>(d.tiles.cols)));
        auto c1 = static_cast<std::size_t>(std::ceil(end_ / std::max(1e-12, d.duration()) * static_cast<double>(d.tiles.cols)));
        c1 = std::clamp(c1, c0 + 1, d.tiles.cols);
        spec.cols = c1 - c0;
        for (std::size_t row = first; row < first + spec.rows; ++row)
            for (std::size_t c = c0; c < c1; ++c) spec.values.push_back(d.tiles.db[row * d.tiles.cols + c]);
        spec.floor = -48;
        spec.ceil = 0;
        Elements labels;
        for (std::size_t i = first; i < first + spec.rows; ++i) {
            labels.push_back(text(band_label(i) + " " + hz_text(d.bands[i].lo_hz) + "–" + hz_text(d.bands[i].hi_hz) + " Гц") |
                             fg(i == selected_ ? th.accent : th.fg, ui_));
            labels.push_back(text(""));
        }
        rows.push_back(hbox({vbox(std::move(labels)) | size(WIDTH, EQUAL, kLabelWidth) | reflect(label_boxes_[bands]),
                             heatmap(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, static_cast<int>(2 * shown)) |
                                 size(WIDTH, EQUAL, plot_width) | reflect(plot_boxes_[0])}));
    } else {
        const bool mra = view_ == View::mra;
        if (mra && r && r->components.empty()) {
            rows.push_back(text(r->mra_skipped.value_or(tr("tui.decomposition.mra_wait"))) | fg(th.warn, ui_));
        } else {
            for (std::size_t i = first; i < first + shown; ++i) {
                const bool on = r && i < r->enabled.size() ? r->enabled[i] : true;
                const auto name = band_label(i);
                PlotSpec spec;
                spec.labels = false;
                spec.x_labels = false;
                spec.x = {begin_, end_};
                spec.playhead = head;
                spec.cursor_a = cursor_;
                const MinMaxPyramid* pyr = nullptr;
                double m = 0;
                std::string amplitude;
                if (mra && r && i < r->component_pyramids.size()) {
                    pyr = &r->component_pyramids[i];
                    const auto all = pyr->query(0, pyr->size(), 1);
                    m = all.empty() ? 0 : std::max(std::abs(all[0].lo), std::abs(all[0].hi));
                    amplitude = "±" + format_number(m, 2);
                } else if (!mra) {
                    pyr = &d.band_pyramids[i];
                    const auto all = pyr->query(0, pyr->size(), 1);
                    m = all.empty() ? 0 : all[0].hi;
                }
                spec.y = {-std::max(m, 1e-12), std::max(m, 1e-12)};
                if (pyr) {
                    const auto [a, b] = samples(pyr->size());
                    const bool mirror = !mra;
                    spec.envelopes.push_back(Envelope{{}, on ? colour(i) : th.dim, [pyr, a, b, mirror](int cols) {
                                                          auto out = pyr->query(a, b, static_cast<std::size_t>(cols));
                                                          if (mirror)
                                                              for (auto& rg : out) rg = {-rg.hi, rg.hi};
                                                          return out;
                                                      }});
                }
                const auto checkbox = clickable("check_" + std::to_string(i), text((on ? on_box : off_box) + " ") | fg(on ? th.fg : th.dim, ui_),
                                                [this, i] { model_->toggle(i, *ui_.jobs); });
                const auto top = hbox({checkbox, text(name) | bold | fg(i == selected_ ? th.accent : colour(i), ui_),
                                       text(" " + hz_text(d.bands[i].lo_hz) + "–" + hz_text(d.bands[i].hi_hz) + " Гц") | fg(th.dim, ui_)});
                const auto bottom = text("   " + (mra ? amplitude + " · " : std::string{}) + pct(d.bands[i].energy_pct) + " · " + group(d.bands[i].length)) |
                                    fg(th.dim, ui_);
                rows.push_back(hbox({vbox({top, bottom}) | size(WIDTH, EQUAL, kLabelWidth) | reflect(label_boxes_[i]),
                                     plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, 2) |
                                         size(WIDTH, EQUAL, plot_width) | reflect(plot_boxes_[i])}));
            }
        }
    }
    if (first + shown < bands) rows.push_back(label_pad(more_row(false, bands - first - shown)) | reflect(label_boxes_[bands + 2]));
    rows.push_back(hbox({text(std::string(kLabelWidth, ' ')), time_axis(plot_width)}));
    return vbox(std::move(rows));
}

ftxui::Element DecompositionScreen::energy_panel(const DecompositionData& d, int max_rows, int width, bool wide) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    constexpr int kName = 3, kRange = 13, kPct = 9, kDb = 10;
    bool show_range = true, show_db = true;
    int bar_width = width - kName - kRange - kPct - kDb;
    if (bar_width < 6) show_db = false, bar_width += kDb;
    if (bar_width < 6) show_range = false, bar_width += kRange;
    bar_width = std::clamp(bar_width, 3, wide ? 400 : 18);
    Elements rows;
    const auto [first, shown] =
        list_window(energy_first_, d.bands.size(), std::max(2, max_rows - 1), 1, follow_ ? std::optional{selected_} : std::nullopt);
    if (first > 0) rows.push_back(more_row(true, first));
    for (std::size_t i = first; i < first + shown; ++i) {
        const auto& b = d.bands[i];
        Elements cells{text(pad_right(band_label(i), kName)) | bold | fg(i == selected_ ? th.accent : th.trace[0], ui_)};
        if (show_range) cells.push_back(text(" " + pad_right(hz_text(b.lo_hz) + "–" + hz_text(b.hi_hz), kRange - 1)) | fg(th.dim, ui_));
        cells.push_back(text(bar(b.energy_pct / 100, bar_width, ui_.caps.glyphs)) | fg(th.trace[0], ui_));
        cells.push_back(text(pad_left(pct(b.energy_pct), kPct)) | fg(th.fg, ui_));
        if (show_db)
            cells.push_back(text(pad_left(b.energy_pct > 0 ? format_fixed(10 * std::log10(b.energy_pct / 100), 1) + " дБ" : "—", kDb)) | fg(th.dim, ui_));
        rows.push_back(hbox(std::move(cells)));
    }
    if (first + shown < d.bands.size()) rows.push_back(more_row(false, d.bands.size() - first - shown));
    const bool exact = d.orthogonal && d.options.mode == core::Extension::periodization && d.parseval_error < 1e-12;
    const auto parseval = exact ? tr("tui.decomposition.parseval_exact") + " " + std::string{glyph(Glyph::check, ui_.caps.glyphs)}
                                : fmt::format(fmt::runtime(tr("tui.decomposition.parseval_approx")), fmt::arg("error", format_number(d.parseval_error, 2)));
    rows.push_back(text(ellipsize(parseval, width, ui_.caps.glyphs == GlyphMode::ascii)) | fg(exact ? th.ok : th.dim, ui_));
    return frame_panel(energy, vbox(std::move(rows)), {}, tr("tui.decomposition.energy"));
}

ftxui::Element DecompositionScreen::cursor_body(const DecompositionData& d, const Reconstruction* r, int max_rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!cursor_) return paragraph(tr("tui.decomposition.cursor_hint")) | fg(th.dim, ui_);
    const auto n = static_cast<std::size_t>(std::min<double>(static_cast<double>(d.x.size() - 1), std::max(0.0, *cursor_ * d.fs)));
    Elements lines{text("x  " + format_number(d.x[n])) | fg(th.fg, ui_)};
    const bool fits = r && !r->components.empty() && std::ranges::all_of(r->components, [n](const auto& c) { return c.size() > n; });
    if (fits) {
        double sum = 0;
        std::string line;
        for (std::size_t i = 0; i < r->components.size(); ++i) {
            sum += r->components[i][n];
            line += pad_right(band_label(i) + " " + format_number(r->components[i][n]), 13);
            if (i % 3 == 2 || i + 1 == r->components.size()) {
                lines.push_back(text(line) | fg(th.fg, ui_));
                line.clear();
            }
        }
        const double err = std::abs(sum - d.x[n]);
        const bool ok = err < 1e-5 * std::max(1.0, std::abs(d.x[n]));
        lines.push_back(text(fmt::format(fmt::runtime(tr("tui.decomposition.sum_error")), fmt::arg("error", format_number(err, 2))) + " " +
                             std::string{glyph(ok ? Glyph::check : Glyph::cross, ui_.caps.glyphs)}) |
                        fg(ok ? th.ok : th.warn, ui_));
    }
    const auto [first, shown] = list_window(cursor_first_, lines.size(), std::max(2, max_rows), 1, std::nullopt);
    Elements rows;
    if (first > 0) rows.push_back(more_row(true, first));
    for (std::size_t i = first; i < first + shown; ++i) rows.push_back(std::move(lines[i]));
    if (first + shown < lines.size()) rows.push_back(more_row(false, lines.size() - first - shown));
    return vbox(std::move(rows));
}

ftxui::Element DecompositionScreen::denoise_panel(const DecompositionData& d, const Reconstruction* r) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto label = [&](const std::string& key) { return text(pad_right(tr(key), 9)) | fg(th.dim, ui_); };
    const bool bayes = !draft_.rule || *draft_.rule == core::ShrinkRule::bayes;
    Elements modes;
    for (auto [mode, name] : {std::pair{core::ThresholdMode::soft, "soft"}, std::pair{core::ThresholdMode::hard, "hard"},
                              std::pair{core::ThresholdMode::garrote, "garrote"}}) {
        const bool on = draft_.mode == mode;
        modes.push_back(clickable(std::string{"mode_"} + name, text(on ? std::string{"["} + name + "]" : std::string{" "} + name + " ") | fg(on ? th.accent : th.fg, ui_),
                                  [this, mode] { draft_.mode = mode; }));
    }
    const auto J = d.options.levels;
    const auto keep_text = draft_.keep_fraction ? "[" + format_number(100 * *draft_.keep_fraction) + " %]" : "[" + tr("tui.decomposition.off") + "]";
    const auto sel = std::min(selected_, d.bands.size() - 1);
    const double gain = sel < draft_.gains_db.size() ? draft_.gains_db[sel] : 0.0;
    std::string lambdas;
    if (r && r->processing.rule)
        for (double l : r->lambdas) lambdas += (lambdas.empty() ? "" : " ") + format_number(l, 2);
    Elements rows{
        hbox({label("tui.decomposition.method"), clickable("rule", text((bayes ? std::string{"BayesShrink"} : std::string{"VisuShrink"}) + " ▾") | fg(th.fg, ui_),
                                                           [this, bayes] { draft_.rule = bayes ? core::ShrinkRule::visu : core::ShrinkRule::bayes; })}),
        hbox({label("tui.decomposition.rule"), hbox(std::move(modes))}),
        hbox({label("tui.decomposition.sigma"), text(format_number(d.sigma) + " ") | fg(th.fg, ui_), text("MAD(d₁)/0,6745") | fg(th.dim, ui_)}),
        hbox({label("tui.decomposition.levels"), text("d₁ … d" + subscript_count(std::min(draft_.last_level, J)) + " ") | fg(th.fg, ui_),
              clickable("levels_down", text("◂") | fg(th.accent, ui_), [this] { draft_.last_level = std::max<std::size_t>(1, draft_.last_level - 1); }),
              clickable("levels_up", text(" ▸") | fg(th.accent, ui_), [this, J] { draft_.last_level = std::min(J, draft_.last_level + 1); })}),
        text(tr(bayes ? "tui.decomposition.lambda_bayes" : "tui.decomposition.lambda_visu")) | fg(th.dim, ui_),
        lambdas.empty() ? emptyElement() : text(ellipsize("λ: " + lambdas, kRightWidth - 4, ui_.caps.glyphs == GlyphMode::ascii)) | fg(th.dim, ui_),
        separator() | fg(th.dim, ui_),
        hbox({label("tui.decomposition.compress"), text(tr("tui.decomposition.keep") + " "),
              clickable("keep", text(keep_text) | fg(th.fg, ui_), [this] {
                  const double now = draft_.keep_fraction.value_or(0);
                  auto it = std::find(kKeep.begin(), kKeep.end(), now);
                  const double next = it == kKeep.end() || it + 1 == kKeep.end() ? kKeep.front() : *(it + 1);
                  draft_.keep_fraction = next > 0 ? std::optional<double>{next} : std::nullopt;
              }),
              text(" " + tr("tui.decomposition.coefficients")) | fg(th.dim, ui_)}),
        hbox({label("tui.decomposition.edit"), text(tr("tui.decomposition.gain") + " " + band_label(sel) + " ") | fg(th.fg, ui_),
              text("[" + std::string{gain >= 0 ? "+" : "−"} + format_number(std::abs(gain)) + " дБ] ") | fg(th.fg, ui_),
              clickable("gain_down", text("◂") | fg(th.accent, ui_), [this, sel] {
                  if (sel < draft_.gains_db.size()) draft_.gains_db[sel] -= 1;
              }),
              clickable("gain_up", text(" ▸") | fg(th.accent, ui_), [this, sel] {
                  if (sel < draft_.gains_db.size()) draft_.gains_db[sel] += 1;
              })}),
        hbox({key_label(" Enter ", ui_), text(" " + tr("tui.decomposition.apply") + "  ") | fg(th.dim, ui_), key_label(" u ", ui_),
              text(" " + tr("tui.decomposition.hint.undo")) | fg(th.dim, ui_)})};
    ftxui::Box* link = nullptr;
    clickables_.push_back({"processing_link", kNoBox, [this] {
                               if (ui_.navigate) ui_.navigate("tab:7", {});
                           }});
    link = &clickables_.back().box;
    return frame_panel(denoise, vbox(std::move(rows)), {{tr("tui.decomposition.processing_link"), false, link}}, tr("tui.decomposition.denoise"));
}

ftxui::Element DecompositionScreen::listen_panel(const DecompositionData& d) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!ui_.player || ui_.player->state() == audio::PlayState::unavailable)
        return frame_panel(listen, text(tr("tui.decomposition.no_audio")) | fg(th.dim, ui_), {}, tr("tui.decomposition.listen"));
    const auto tag = ui_.player->tag();
    const bool playing = playhead().has_value();
    auto button = [&](std::string id, std::string label, ClipKind kind, const char* tag_name) {
        const bool on = playing && tag == tag_name;
        return clickable(std::move(id), text("▶ " + label) | fg(on ? th.accent : th.fg, ui_) | (on ? bold : nothing), [this, kind] { play(kind); });
    };
    const double pos = playhead().value_or(cursor_.value_or(0));
    Elements rows{hbox({button("play_a", tr("tui.decomposition.play_a"), ClipKind::original, "decomposition.original"), text("  "),
                        button("play_b", tr("tui.decomposition.play_b"), ClipKind::enabled, "decomposition.enabled")}),
                  hbox({button("play_x", tr("tui.decomposition.play_x"), ClipKind::difference, "decomposition.difference"), text("  "),
                        button("play_d", fmt::format(fmt::runtime(tr("tui.decomposition.play_band")), fmt::arg("band", band_label(selected_))),
                               ClipKind::band, "decomposition.band")}),
                  hbox({text(bar(d.duration() > 0 ? pos / d.duration() : 0, 22, ui_.caps.glyphs)) | fg(th.accent, ui_),
                        text(" " + format_fixed(pos, 2) + " с") | fg(th.dim, ui_)}),
                  text(tr("tui.decomposition.shared_cursor")) | fg(th.dim, ui_)};
    if (!notice_.empty()) rows.push_back(text(notice_) | fg(th.warn, ui_));
    if (auto e = model_->error()) rows.push_back(text(*e) | fg(th.err, ui_));
    else if (model_->cancelled()) rows.push_back(text(tr("tui.decomposition.cancelled")) | fg(th.warn, ui_));
    return frame_panel(listen, vbox(std::move(rows)), {}, tr("tui.decomposition.listen"));
}

ftxui::Element DecompositionScreen::summary_panel(const DecompositionData& d, const Reconstruction* r) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    auto label = [&](const std::string& key) { return text(pad_right(tr(key), 13)) | fg(th.dim, ui_); };
    std::size_t off = 0;
    std::string included;
    if (r)
        for (std::size_t i = 0, n = r->enabled.size(); i < n;) {
            if (!r->enabled[i]) {
                ++off, ++i;
                continue;
            }
            auto j = i;
            while (j + 1 < n && r->enabled[j + 1]) ++j;
            const auto part = j == i ? band_label(i) : j == i + 1 ? band_label(i) + ", " + band_label(j) : band_label(i) + "–" + band_label(j);
            included += (included.empty() ? "" : ", ") + part;
            i = j + 1;
        }
    if (off == 0) included = fmt::format(fmt::runtime(tr("tui.decomposition.all_bands")), fmt::arg("n", d.bands.size()));
    const bool pr_ok = d.pr_error < 1e-9;
    const auto pr_text = d.pr_error < 1e-14 ? std::string{"< 10⁻¹⁴"} : format_number(d.pr_error, 2);
    Elements rows{hbox({label("tui.decomposition.in_synthesis"), paragraph(included) | fg(th.fg, ui_) | flex}),
                  hbox({label("tui.decomposition.energy_kept"), text(r ? pct(r->energy_kept_pct) : "—") | fg(th.fg, ui_),
                        text(" " + tr("tui.decomposition.of_original")) | fg(th.dim, ui_)}),
                  hbox({label("tui.decomposition.pr_all"), text(tr("tui.decomposition.error") + " " + pr_text + " ") | fg(th.fg, ui_),
                        text(std::string{glyph(pr_ok ? Glyph::check : Glyph::cross, ui_.caps.glyphs)}) | fg(pr_ok ? th.ok : th.err, ui_)}),
                  hbox({label("tui.decomposition.parseval"), text("Σ‖dⱼ‖² + ‖aⱼ‖² ≈ ‖x‖²") | fg(th.fg, ui_)}),
                  text(std::string(13, ' ') + fmt::format(fmt::runtime(tr("tui.decomposition.parseval_note")), fmt::arg("error", format_number(d.parseval_error, 2)))) |
                      fg(th.dim, ui_),
                  separator() | fg(th.dim, ui_)};
    if (off > 0) {
        rows.push_back(text(fmt::format(fmt::runtime(tr("tui.decomposition.warn1")), fmt::arg("n", off))) | fg(th.warn, ui_));
        rows.push_back(text(tr("tui.decomposition.warn2")) | fg(th.warn, ui_));
        rows.push_back(text(tr("tui.decomposition.warn3")) | fg(th.dim, ui_));
    } else {
        rows.push_back(text(tr("tui.decomposition.all_on")) | fg(th.ok, ui_));
        rows.push_back(text(tr("tui.decomposition.all_on_hint")) | fg(th.dim, ui_));
    }
    return frame_panel(summary, vbox(std::move(rows)), {}, tr("tui.decomposition.summary"));
}

ftxui::Element DecompositionScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    clickables_.clear();
    if (!ui_.jobs) return text("");
    model_->sync(*ui_.jobs);
    const auto d = model_->data();
    const auto r = model_->recon();
    sync_view(d);
    if (ui_.ticker) ui_.ticker->want("decomposition", playhead().has_value());
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const auto& th = ui_.theme;
    struct FollowOnce {
        bool& follow;
        ~FollowOnce() { follow = false; }
    } follow_once{follow_};
    if (!d) {
        Elements lines;
        if (auto e = model_->error()) lines.push_back(text(*e) | fg(th.err, ui_));
        else if (model_->cancelled()) lines.push_back(text(tr("tui.decomposition.cancelled")) | fg(th.warn, ui_));
        else if (model_->source()) lines.push_back(text(tr("tui.decomposition.computing")) | fg(th.dim, ui_));
        else lines.push_back(text(tr("tui.decomposition.empty")) | fg(th.fg, ui_));
        return frame_panel(view_panel, centered(std::move(lines)), {}, tr("tui.decomposition.title.subbands"));
    }
    const auto title = view_ == View::subbands ? tr("tui.decomposition.title.subbands")
                       : view_ == View::mra    ? fmt::format(fmt::runtime(tr("tui.decomposition.title.mra")), fmt::arg("a", band_label(d->bands.size() - 1)))
                                               : tr("tui.decomposition.title.tiles");
    const std::vector<Chip> view_chips{{tr(view_ == View::tiles ? "tui.decomposition.chip.tiles" : "tui.decomposition.chip.scale"), false}};
    const bool small = breakpoint_ <= Breakpoint::small || maximized_;
    const int main_width = small ? cols : cols - kRightWidth - 1;
    const bool two_rows = cols < 150;
    const int toolbar_rows = two_rows ? 2 : 1;
    const int total = std::max(8, rows - 3);
    const int body_rows = small ? total - toolbar_rows - 2 : std::max(6, total - toolbar_rows - 1 - 2 - 1 - 9);
    const auto body = view_body(*d, r.get(), main_width, body_rows);
    const int used = std::min(body_rows, static_cast<int>(2 * std::min(d->bands.size(), static_cast<std::size_t>(body_rows))) + 3);
    const int energy_rows = small ? total - 2 : std::max(4, total - toolbar_rows - 1 - (used + 2) - 1 - 2);
    const int energy_width = small ? cols - 4 : main_width - 47;
    const bool wide = maximized_.has_value();
    auto main = frame_panel(view_panel, body, view_chips, title);
    if (small) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case view_panel: return vbox({toolbar(two_rows), main | flex});
        case energy: return energy_panel(*d, energy_rows, energy_width, wide);
        case cursor_panel: return frame_panel(cursor_panel, cursor_body(*d, r.get(), total - 2), {}, tr("tui.decomposition.cursor"));
        case denoise: return denoise_panel(*d, r.get());
        case listen: return listen_panel(*d);
        case summary: return summary_panel(*d, r.get());
        case panel_count: break;
        }
    }
    const auto cursor_title = cursor_ ? fmt::format(fmt::runtime(tr("tui.decomposition.cursor_at")), fmt::arg("t", format_fixed(*cursor_, 3)))
                                      : tr("tui.decomposition.cursor");
    auto bottom = hbox({energy_panel(*d, energy_rows, energy_width, wide) | flex, text(" "),
                        frame_panel(cursor_panel, cursor_body(*d, r.get(), energy_rows), {}, cursor_title) | size(WIDTH, EQUAL, 42)});
    auto left = vbox({main, text(""), bottom | flex});
    auto right = vbox({denoise_panel(*d, r.get()), text(""), listen_panel(*d), text(""), summary_panel(*d, r.get()) | flex});
    return vbox({toolbar(two_rows), text(""), hbox({left | flex, text(" "), right | size(WIDTH, EQUAL, kRightWidth)}) | flex});
}

void DecompositionScreen::select(std::size_t band) {
    selected_ = band;
    follow_ = true;
}

bool DecompositionScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    using ftxui::Mouse;
    if (!ui_.jobs) return false;
    auto& jobs = *ui_.jobs;
    const auto d = model_->data();
    if (e.is_mouse()) {
        const auto plot_box_ = plot_area();
        const auto& m = e.mouse();
        if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
            const bool down = m.button == Mouse::WheelDown;
            const auto scroll = [down](std::size_t& first) {
                if (down) ++first;
                else if (first > 0) --first;
                return true;
            };
            if (std::ranges::any_of(label_boxes_, [&](const ftxui::Box& b) { return b.Contain(m.x, m.y); })) return scroll(band_first_);
            if (panel_boxes_[energy].Contain(m.x, m.y)) return scroll(energy_first_);
            if (panel_boxes_[cursor_panel].Contain(m.x, m.y)) return scroll(cursor_first_);
        }
        if ((m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) && plot_box_.Contain(m.x, m.y)) {
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
            if (plot_box_.Contain(m.x, m.y)) {
                for (std::size_t i = 0; i < plot_boxes_.size(); ++i)
                    if (plot_boxes_[i].Contain(m.x, m.y) && view_ != View::tiles) select(i);
                drag_ = {true, false, m.x};
                return true;
            }
            return true;
        }
        if (drag_.active) {
            if (m.motion == Mouse::Released) {
                if (!drag_.moved) set_cursor(time_at(m.x));
                drag_.active = false;
                return true;
            }
            if (m.x != drag_.last_x) {
                drag_.moved = true;
                const int width = std::max(1, plot_box_.x_max - plot_box_.x_min + 1);
                pan((drag_.last_x - m.x) * (end_ - begin_) / width);
                drag_.last_x = m.x;
            }
            return true;
        }
        return false;
    }
    if (e == Event::Escape) {
        if (playhead()) ui_.player->stop();
        else if (model_->job()) model_->cancel(jobs);
        else if (maximized_) maximized_.reset();
        else return false;
        return true;
    }
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
        return true;
    }
    if (e == Event::Character('r') && !model_->job()) return model_->recompute(jobs), true;
    if (e == Event::Character('o') && ui_.ask_open) return ui_.ask_open(), true;
    if (!d) return false;
    if ((e == Event::PageDown || e == Event::PageUp) && focus_ == cursor_panel) {
        if (e == Event::PageDown) cursor_first_ += 3;
        else cursor_first_ = cursor_first_ > 3 ? cursor_first_ - 3 : 0;
        return true;
    }
    if (e == Event::ArrowDown) return select(std::min(selected_ + 1, d->bands.size() - 1)), true;
    if (e == Event::ArrowUp) return select(selected_ > 0 ? selected_ - 1 : 0), true;
    if (e == Event::PageDown) return select(std::min(selected_ + 5, d->bands.size() - 1)), true;
    if (e == Event::PageUp) return select(selected_ > 5 ? selected_ - 5 : 0), true;
    if (e == Event::ArrowLeft || e == Event::ArrowRight) {
        const int width = std::max(1, plot_area().x_max - plot_area().x_min + 1);
        const double step = (end_ - begin_) / width;
        const double t = cursor_.value_or((begin_ + end_) / 2) + (e == Event::ArrowLeft ? -step : step);
        set_cursor(t);
        if (*cursor_ < begin_) pan(*cursor_ - begin_);
        if (*cursor_ > end_) pan(*cursor_ - end_);
        return true;
    }
    if (e == Event::Home) return set_cursor(begin_), true;
    if (e == Event::End) return set_cursor(end_), true;
    if (e == Event::Character('+') || e == Event::Character('=') || e == Event::Character('-')) {
        const double anchor = cursor_ && *cursor_ >= begin_ && *cursor_ <= end_ ? *cursor_ : (begin_ + end_) / 2;
        zoom(e == Event::Character('-') ? 1 / kWheelStep : kWheelStep, anchor);
        return true;
    }
    if (e == Event::Character('>') || e == Event::Character('.')) return pan(0.2 * (end_ - begin_)), true;
    if (e == Event::Character('<') || e == Event::Character(',')) return pan(-0.2 * (end_ - begin_)), true;
    if (e == Event::Character('0')) return zoom(1e-12, begin_), true;
    if (e == Event::Character('j') || e == Event::Character('J')) {
        auto k = model_->options();
        if (e == Event::Character('j')) ++k.levels;
        else if (k.levels > 1) --k.levels;
        model_->set_options(k, jobs);
        return true;
    }
    if (e == Event::Character('e') || e == Event::Character('E')) {
        auto k = model_->options();
        auto it = std::find(kModes.begin(), kModes.end(), k.mode);
        if (e == Event::Character('e')) k.mode = it == kModes.end() || it + 1 == kModes.end() ? kModes.front() : *(it + 1);
        else k.mode = it == kModes.begin() || it == kModes.end() ? kModes.back() : *(it - 1);
        model_->set_options(k, jobs);
        return true;
    }
    if (e == Event::Character('w')) {
        if (ui_.open_palette) ui_.open_palette(tr("tui.decomposition.cmd.wavelet"));
        return true;
    }
    if (e == Event::Return) {
        if (focus_ == denoise) model_->apply(draft_, jobs);
        else model_->toggle(selected_, jobs);
        return true;
    }
    if (e == Event::Character('u')) return model_->undo(jobs), true;
    if (e == Event::Character('v')) {
        view_ = view_ == View::subbands ? View::mra : view_ == View::mra ? View::tiles : View::subbands;
        return true;
    }
    if (e == Event::Character(' ')) {
        if (playhead()) ui_.player->stop();
        else play(ClipKind::enabled);
        return true;
    }
    if (e == Event::Character('a')) return play(ClipKind::original), true;
    if (e == Event::Character('b')) return play(ClipKind::enabled), true;
    if (e == Event::Character('x')) return play(ClipKind::difference), true;
    if (e == Event::Character('d')) return play(ClipKind::band), true;
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

void DecompositionScreen::open_with(const NavArgs& args) {
    if (args.wavelet.empty() || !ui_.jobs) return;
    auto o = model_->options();
    o.wavelet = args.wavelet;
    model_->set_options(o, *ui_.jobs);
}

ftxui::Box DecompositionScreen::area(std::string_view id) const {
    if (id == "view_plot") return plot_area();
    if (id == "denoise_panel") return panel_boxes_[denoise];
    if (id == "energy_panel") return panel_boxes_[energy];
    if (id == "cursor_panel") return panel_boxes_[cursor_panel];
    if (id == "band_labels") {
        ftxui::Box out = kNoBox;
        for (const auto& b : label_boxes_)
            if (valid(b)) out = valid(out) ? ftxui::Box::Union(out, b) : b;
        return out;
    }
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

std::vector<JumpTarget> DecompositionScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
}

std::vector<Command> DecompositionScreen::commands() {
    std::vector<Command> out;
    for (auto name : core::wavelet_names())
        out.push_back({"decomposition.wavelet." + std::string{name}, tr("tui.decomposition.cmd.wavelet") + " " + std::string{name}, [this, n = std::string{name}] {
                           if (!ui_.jobs) return;
                           auto o = model_->options();
                           o.wavelet = n;
                           model_->set_options(o, *ui_.jobs);
                       }});
    return out;
}

std::optional<std::pair<int, int>> DecompositionScreen::menu_anchor() const {
    const auto& b = panel_boxes_[static_cast<std::size_t>(focus_)];
    if (b.x_max < b.x_min) return std::nullopt;
    return std::pair{(b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2};
}

std::shared_ptr<TabScreen> make_decomposition_screen(UiContext& ui, std::shared_ptr<DecompositionModel> model) {
    return std::make_shared<DecompositionScreen>(ui, std::move(model));
}

}  // namespace signals::tui
