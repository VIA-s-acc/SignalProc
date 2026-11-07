#include "tui/screens/matrix_screen.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/design/families.hpp"
#include "i18n/i18n.hpp"
#include "core/matrix.hpp"
#include "tui/screens/publish_util.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/heatmap.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kLeftWidth = 32;
constexpr int kNumberWidth = 6;
constexpr double kWheelStep = 1.25;
constexpr auto kAnimationStep = std::chrono::milliseconds{500};
constexpr std::array<core::Extension, 3> kModes{core::Extension::periodization, core::Extension::symmetric, core::Extension::zero};

std::string signed_fixed(double v, int decimals) { return (v < 0 ? "−" : "+") + format_fixed(std::abs(v), decimals); }

std::string check_value(const std::optional<double>& v) {
    if (!v) return "—";
    return *v < 1e-14 ? "< 10⁻¹⁴" : format_number(*v, 2);
}

}  // namespace

MatrixScreen::MatrixScreen(UiContext& ui, std::shared_ptr<MatrixModel> model) : ui_{ui}, model_{std::move(model)} {
    panel_boxes_.fill(kNoBox);
    matrix_box_ = kNoBox;
}

std::string MatrixScreen::tab_label() const { return tr("tui.tab.matrix"); }

std::optional<TheoryLink> MatrixScreen::theory_link() const {
    const auto op = model_->options().op;
    return TheoryLink{TheoryLink::Kind::card,
                      op == MatrixOp::convolution || op == MatrixOp::circulant || op == MatrixOp::toeplitz_hankel ? "toeplitz" : "bank_matrix"};
}

void MatrixScreen::publish(theory::LiveValues& v) const {
    const auto d = model_->data();
    const auto& o = model_->options();
    v.source = tab_label() + " ▸ " + o.wavelet + " · N = " + std::to_string(o.n);
    v.set("block.bank", o.wavelet);
    v.set("block.n", std::to_string(o.n));
    v.set("block.mode", std::string{core::to_string(o.mode)});
    v.set("block.order", o.order == RowOrder::blocks ? std::string{"[L; B]"} : tr("tui.matrix.interleaved"));
    v.set("block.filter", o.wavelet + " h₀");
    if (!d || d->m.size() == 0) return;
    v.set("block.rows", std::to_string(d->m.rows()));
    v.set("block.cols", std::to_string(d->m.cols()));
    v.set("block.band_width", std::to_string(d->filter_length));
    v.set("metric.cond", d->cond > 0 ? format_number(d->cond, 3) : std::string{"—"});
    if (d->orth_error) v.set_check({"check.ata_identity", o.wavelet, *d->orth_error, 1e-10});
    else v.set("check.ata_identity", d->square ? std::string{"—"} : tr("tui.theory.value.not_square"));
    if (d->left_inverse_error) v.set_check({"check.sa_identity", o.wavelet, *d->left_inverse_error, 1e-10});
    else v.set("check.sa_identity", "—");
    if (const auto bank = core::wavelet(o.wavelet)) v.set_check(core::check_circulant_fft(bank->h0, o.n, 1e-10));
}

std::vector<Hint> MatrixScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},   {"↑↓ PgUp PgDn Home End", tr("tui.matrix.hint.row")}, {"←→", tr("tui.matrix.hint.column")},
            {"v/V", tr("tui.matrix.hint.operation")}, {"+ − 0 < >", tr("tui.hint.plot_zoom")},
            {"Space", tr("tui.matrix.hint.animate")}, {"c", tr("tui.matrix.hint.numbers")},
            {"s", tr("tui.matrix.hint.spectrum")},
            {"t", "AᵀA"},                           {"b", tr("tui.matrix.hint.edges")},       {"o", tr("tui.matrix.hint.order")},
            {"n/N", "N"},                           {tr("tui.signal.key.click"), tr("tui.matrix.hint.cell")},
            {"z", tr("tui.signal.hint.zoom")},      {tr("tui.signal.key.wheel"), tr("tui.zoom.hint")},
            {"F1", tr("tui.matrix.hint.f1")}};
}

std::string MatrixScreen::context() const {
    const auto o = model_->options();
    return fmt::format("{} · N = {} · {}", o.wavelet, o.n, core::to_string(o.mode));
}

std::chrono::steady_clock::time_point MatrixScreen::now() const { return ui_.now ? ui_.now() : std::chrono::steady_clock::now(); }

void MatrixScreen::apply(MatrixOptions o) {
    if (!ui_.jobs) return;
    model_->set(std::move(o), *ui_.jobs);
}

int MatrixScreen::max_numbers_n(int width) const { return std::max(2, (width - 8) / kNumberWidth); }

ftxui::Element MatrixScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1),
                                     fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element MatrixScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), kNoBox, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element MatrixScreen::controls_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto o = model_->options();
    auto chip = [&](std::string id, std::string label, bool on, std::function<void()> action) {
        return clickable(std::move(id), text(on ? "[" + label + "]" : " " + label + " ") | fg(on ? th.accent : th.fg, ui_) | (on ? bold : nothing),
                         std::move(action));
    };
    Elements rows{hbox({text(pad_right(tr("tui.matrix.filter"), 9)) | fg(th.dim, ui_), clickable("filter", text(o.wavelet + " ▾") | fg(th.fg, ui_), [this] {
                            if (ui_.open_palette) ui_.open_palette(tr("tui.matrix.cmd.filter"));
                        })}),
                  text(tr("tui.matrix.operation")) | fg(th.dim, ui_)};
    const std::pair<MatrixOp, const char*> ops[]{{MatrixOp::analysis, "analysis"},
                                                 {MatrixOp::multilevel, "multilevel"},
                                                 {MatrixOp::convolution, "convolution"},
                                                 {MatrixOp::circulant, "circulant"},
                                                 {MatrixOp::synthesis_product, "product"},
                                                 {MatrixOp::toeplitz_hankel, "toeplitz_hankel"}};
    for (const auto& [op, key] : ops)
        rows.push_back(hbox({text(" "), chip(std::string{"op_"} + key, tr(std::string{"tui.matrix.op."} + key), o.op == op, [this, op] {
                                 auto k = model_->options();
                                 k.op = op;
                                 apply(k);
                             })}));
    rows.push_back(text(tr("tui.matrix.edges")) | fg(th.dim, ui_));
    Elements modes{text(" ")};
    const std::pair<core::Extension, const char*> labels[]{{core::Extension::zero, "zero"}, {core::Extension::periodization, "period."},
                                                           {core::Extension::symmetric, "symm."}};
    for (const auto& [mode, label] : labels)
        modes.push_back(chip(std::string{"mode_"} + label, label, o.mode == mode, [this, mode] {
            auto k = model_->options();
            k.mode = mode;
            apply(k);
        }));
    rows.push_back(hbox(std::move(modes)));
    rows.push_back(text(tr("tui.matrix.order")) | fg(th.dim, ui_));
    rows.push_back(hbox({text(" "), chip("order_blocks", "L; B", o.order == RowOrder::blocks, [this] {
                             auto k = model_->options();
                             k.order = RowOrder::blocks;
                             apply(k);
                         }),
                         chip("order_interleaved", tr("tui.matrix.interleaved"), o.order == RowOrder::interleaved, [this] {
                             auto k = model_->options();
                             k.order = RowOrder::interleaved;
                             apply(k);
                         })}));
    rows.push_back(hbox({text(pad_right("N", 9)) | fg(th.dim, ui_), text(std::to_string(o.n) + " ") | fg(th.fg, ui_),
                         clickable("n_less", text("◂") | fg(th.accent, ui_), [this] {
                             auto k = model_->options();
                             k.n = k.n > 2 ? k.n - 2 : 2;
                             apply(k);
                         }),
                         clickable("n_more", text("▸") | fg(th.accent, ui_), [this] {
                             auto k = model_->options();
                             k.n += 2;
                             apply(k);
                         })}));
    rows.push_back(hbox({text(pad_right(tr("tui.matrix.cell"), 9)) | fg(th.dim, ui_),
                         clickable("cell_mode", text(numbers_ ? tr("tui.matrix.cell_numbers") : tr("tui.matrix.cell_colour")) | fg(th.fg, ui_),
                                   [this] { numbers_ = !numbers_; })}));
    rows.push_back(text(fmt::format(fmt::runtime(tr("tui.matrix.numbers_limit")), fmt::arg("n", max_numbers_n(main_width_)))) | fg(th.dim, ui_));
    return frame_panel(controls, vbox(std::move(rows)), {}, tr("tui.matrix.controls"));
}

ftxui::Element MatrixScreen::checks_panel(const MatrixData* d) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!d) return frame_panel(checks, text("…") | fg(th.dim, ui_), {}, tr("tui.matrix.checks"));
    Elements rows;
    if (d->note) rows.push_back(paragraph(*d->note) | fg(th.warn, ui_));
    if (d->error) {
        rows.push_back(paragraph(*d->error) | fg(th.err, ui_));
        return frame_panel(checks, vbox(std::move(rows)), {}, tr("tui.matrix.checks"));
    }
    auto mark = [&](const std::optional<double>& v, double tol) {
        const bool ok = v && *v <= tol;
        return text(" " + std::string{glyph(ok ? Glyph::check : Glyph::cross, ui_.caps.glyphs)}) | fg(ok ? th.ok : th.err, ui_);
    };
    if (d->orth_error) rows.push_back(hbox({text("‖AᵀA − I‖max = " + check_value(d->orth_error)), mark(d->orth_error, 1e-12)}));
    if (d->transpose_error) {
        if (*d->transpose_error < 1e-12) rows.push_back(text("S = Aᵀ " + tr("tui.matrix.orthogonal_bank")) | fg(th.fg, ui_));
        else rows.push_back(text("‖S − Aᵀ‖ = " + check_value(d->transpose_error)) | fg(th.fg, ui_));
    }
    if (d->left_inverse_error) rows.push_back(hbox({text("‖S·A·x − x‖ = " + check_value(d->left_inverse_error)), mark(d->left_inverse_error, 1e-12)}));
    if (d->dct_error) rows.push_back(hbox({text(fmt::format(fmt::runtime(tr("tui.matrix.dct_check")), fmt::arg("value", check_value(d->dct_error)))), mark(d->dct_error, 1e-12)}));
    if (d->levinson_error)
        rows.push_back(hbox({text(fmt::format(fmt::runtime(tr("tui.matrix.levinson_check")), fmt::arg("value", check_value(d->levinson_error)))), mark(d->levinson_error, 1e-10)}));
    if (!d->singular.empty())
        rows.push_back(text(fmt::format(fmt::runtime(tr("tui.matrix.singular")), fmt::arg("hi", format_number(d->singular.front())),
                                        fmt::arg("lo", format_number(d->singular.back())), fmt::arg("cond", format_number(d->cond)))) |
                       fg(th.fg, ui_));
    rows.push_back(separator() | fg(th.dim, ui_));
    if (!d->square && (d->options.op == MatrixOp::analysis || d->options.op == MatrixOp::multilevel)) {
        rows.push_back(text(fmt::format(fmt::runtime(tr("tui.matrix.nonsquare_head")), fmt::arg("mode", core::to_string(d->options.mode)),
                                        fmt::arg("wavelet", d->options.wavelet))) |
                       fg(th.dim, ui_));
        for (const char* key : {"tui.matrix.nonsquare1", "tui.matrix.nonsquare2", "tui.matrix.nonsquare3"}) rows.push_back(text(tr(key)) | fg(th.warn, ui_));
    }
    return frame_panel(checks, vbox(std::move(rows)), {}, tr("tui.matrix.checks"));
}

ftxui::Element MatrixScreen::row_info(const MatrixData& d) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto r = std::min<std::size_t>(row_, static_cast<std::size_t>(std::max<Eigen::Index>(1, d.m.rows()) - 1));
    Elements out{hbox({clickable("row_prev", text("◂ " + tr("tui.matrix.row")) | fg(th.fg, ui_), [this] { row_ = row_ > 0 ? row_ - 1 : 0; }), text("  "),
                       clickable("row_next", text(tr("tui.matrix.row") + " ▸") | fg(th.fg, ui_), [this] { ++row_; })}),
                 text("")};
    if (r < d.row_names.size()) {
        std::string kind = tr("tui.matrix.kind.row");
        const bool analysis = d.options.op == MatrixOp::analysis && !d.options.gram;
        const bool detail = d.options.order == RowOrder::blocks ? r >= d.detail_start : r % 2 == 1;
        if (analysis) kind = tr(detail ? "tui.matrix.kind.high" : "tui.matrix.kind.low");
        out.push_back(text(d.row_names[r] + " · " + kind) | bold | fg(th.fg, ui_));
        if (analysis) {
            const auto len = d.detail_start > 0 && d.options.order == RowOrder::blocks ? d.detail_start : (d.row_names.size() / 2);
            const auto k = d.options.order == RowOrder::blocks ? (detail ? r - d.detail_start : r) : r / 2;
            (void)len;
            out.push_back(text(fmt::format(fmt::runtime(tr("tui.matrix.shift")), fmt::arg("filter", detail ? "h₁" : "h₀"), fmt::arg("k", k),
                                           fmt::arg("shift", 2 * k))) |
                          fg(th.dim, ui_));
        }
    }
    out.push_back(text(""));
    out.push_back(text(tr("tui.matrix.dot")) | fg(th.dim, ui_));
    const auto terms = model_->row_terms(r);
    const std::size_t terms_limit = maximized_ ? terms.size() : 8;
    double total = 0;
    for (std::size_t i = 0; i < terms.size(); ++i) {
        total += terms[i].product;
        if (i < terms_limit)
            out.push_back(text("  " + signed_fixed(terms[i].coef, 4) + " · x" + subscript_count(terms[i].column) + " (" + format_number(terms[i].x) + ")  = " +
                               signed_fixed(terms[i].product, 3)) |
                          fg(th.fg, ui_));
    }
    if (terms.size() > terms_limit)
        out.push_back(text(fmt::format(fmt::runtime(tr("tui.matrix.more_terms")), fmt::arg("n", terms.size() - terms_limit))) | fg(th.dim, ui_));
    out.push_back(hbox({text("  = "), text(signed_fixed(total, 4)) | bold | fg(th.accent, ui_),
                        text(r < d.outputs.size() ? "  = " + d.outputs[r] : std::string{}) | fg(th.dim, ui_)}));
    out.push_back(text(""));
    out.push_back(text("x[n] = round(8·sin(2πn/16) + n/2)") | fg(th.dim, ui_));
    out.push_back(text(tr("tui.matrix.note1")) | fg(th.dim, ui_));
    out.push_back(text(tr("tui.matrix.note2")) | fg(th.dim, ui_));
    if (!notice_.empty()) out.push_back(text(notice_) | fg(th.accent, ui_));
    return vbox(std::move(out));
}

ftxui::Element MatrixScreen::main_panel(const MatrixData* d, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (!d) return frame_panel(main, text("…") | fg(th.dim, ui_), {}, tr("tui.matrix.title_empty"));
    if (d->error || d->m.size() == 0)
        return frame_panel(main, paragraph(d->error.value_or("—")) | fg(th.err, ui_), {}, tr("tui.matrix.title_empty"));
    const auto rows = static_cast<std::size_t>(d->m.rows()), cols = static_cast<std::size_t>(d->m.cols());
    row_ = std::min(row_, rows - 1);
    update_block(rows, cols);
    const auto [r0, nr, c0, nc] = block_;
    const bool zoomed = nr < rows || nc < cols;
    std::string name = "A = [L; B]";
    switch (d->options.op) {
    case MatrixOp::analysis: name = d->options.order == RowOrder::blocks ? "A = [L; B]" : tr("tui.matrix.name.interleaved"); break;
    case MatrixOp::multilevel: name = "W"; break;
    case MatrixOp::convolution: name = "T(h₀)"; break;
    case MatrixOp::circulant: name = "C(h₀)"; break;
    case MatrixOp::synthesis_product: name = "S·A"; break;
    case MatrixOp::toeplitz_hankel: name = "T + H"; break;
    }
    const auto base_name = name;
    if (d->options.gram) name = d->options.op == MatrixOp::toeplitz_hankel ? "C(T+H)Cᵀ" : "AᵀA";
    if (spectrum_ && !d->singular.empty()) {
        tui::Trace t{};
        t.stems = d->singular.size() <= 64;
        t.color = th.trace[0];
        double lo = 0, hi = -300;
        for (std::size_t k = 0; k < d->singular.size(); ++k) {
            const double v = std::log10(std::max(d->singular[k], 1e-300));
            t.x.push_back(static_cast<double>(k));
            t.y.push_back(v);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        lo = std::max(lo, hi - 17);
        for (auto& y : t.y) y = std::max(y, lo);
        PlotSpec spec;
        spec.x = sigma_zoom_.axis("sigma", {-0.5, static_cast<double>(d->singular.size()) - 0.5}).axis();
        spec.y = {lo - 0.2, hi + 0.2};
        spec.traces.push_back(std::move(t));
        auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
        const auto title = fmt::format(fmt::runtime(tr("tui.matrix.spectrum_title")), fmt::arg("name", base_name), fmt::arg("count", d->singular.size()));
        return frame_panel(main, vbox({text("log₁₀ σᵢ") | fg(th.dim, ui_), sigma_zoom_.track("sigma", std::move(spec), std::move(drawn)) | flex}), {}, title);
    }
    auto title = fmt::format(fmt::runtime(tr("tui.matrix.title")), fmt::arg("name", name), fmt::arg("rows", rows), fmt::arg("cols", cols),
                             fmt::arg("row", row_));
    if (zoomed)
        title += fmt::format(fmt::runtime(tr("tui.matrix.zoomed")), fmt::arg("r0", r0), fmt::arg("r1", r0 + nr - 1), fmt::arg("c0", c0),
                             fmt::arg("c1", c0 + nc - 1));
    const int info_width = 44;
    const int room_w = std::max(8, width - info_width - 10), room_h = std::max(2, height - 4);
    const int scale = std::max(1, std::min(room_w / static_cast<int>(2 * nc), 2 * room_h / static_cast<int>(nr)));
    const int map_width = std::max(8, std::min(static_cast<int>(2 * nc) * scale, room_w));
    const bool fits_numbers = static_cast<int>(nc) <= max_numbers_n(width - info_width);
    Element map;
    if (numbers_ && fits_numbers) {
        Elements lines;
        for (std::size_t r = r0; r < r0 + nr && static_cast<int>(lines.size()) < height - 4; ++r) {
            Elements cells{text(pad_right(r < d->row_names.size() ? d->row_names[r] : std::string{}, 5)) | fg(th.dim, ui_)};
            for (std::size_t c = c0; c < c0 + nc; ++c) {
                const double v = d->m(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c));
                auto cell = text(pad_left(std::abs(v) < 1e-12 ? "·" : signed_fixed(v, 2), kNumberWidth)) |
                            fg(std::abs(v) < 1e-12 ? th.dim : v > 0 ? th.trace[0] : th.trace[1], ui_);
                if (r == row_) cell = cell | bg(highlight_color(th), ui_);
                cells.push_back(cell);
            }
            lines.push_back(hbox(std::move(cells)));
        }
        map = vbox(std::move(lines)) | reflect(matrix_box_);
    } else {
        HeatmapSpec spec;
        spec.rows = nr;
        spec.cols = nc;
        spec.scale = HeatScale::diverging;
        if (row_ >= r0 && row_ < r0 + nr) spec.highlight_row = row_ - r0;
        spec.values.reserve(nr * nc);
        for (std::size_t r = r0; r < r0 + nr; ++r)
            for (std::size_t c = c0; c < c0 + nc; ++c) spec.values.push_back(d->m(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)));
        const int map_height = std::max(2, std::min(static_cast<int>((nr * static_cast<std::size_t>(scale) + 1) / 2), room_h));
        std::string header = "     ";
        const std::size_t label_step = std::max<std::size_t>(1, nc / std::max<std::size_t>(1, static_cast<std::size_t>(map_width / 8)));
        for (std::size_t k = 0; k < nc && static_cast<int>(header.size()) < map_width + 5; k += std::max<std::size_t>(label_step, zoomed ? 1 : 8)) {
            const auto label = "x" + subscript_count(c0 + k);
            const int cell = std::max(1, map_width * static_cast<int>(std::max<std::size_t>(label_step, zoomed ? 1 : 8)) / static_cast<int>(nc));
            header += pad_right(label, std::min(16, std::max(cell, 1)));
        }
        Elements labels(static_cast<std::size_t>(map_height), text(""));
        auto put_label = [&](std::size_t r) {
            if (r < r0 || r >= r0 + nr) return;
            const auto line = static_cast<std::size_t>((r - r0) * static_cast<std::size_t>(map_height) / std::max<std::size_t>(1, nr));
            if (line < labels.size() && r < d->row_names.size()) labels[line] = text(d->row_names[r]) | fg(th.dim, ui_);
        };
        put_label(r0);
        if (d->detail_start > 0 && d->detail_start < rows) {
            put_label(d->detail_start - 1);
            put_label(d->detail_start);
        }
        put_label(r0 + nr - 1);
        if (row_ >= r0 && row_ < r0 + nr) {
            const auto marker = static_cast<std::size_t>((row_ - r0) * static_cast<std::size_t>(map_height) / std::max<std::size_t>(1, nr));
            if (marker < labels.size()) labels[marker] = text(std::string{ui_.caps.glyphs == GlyphMode::ascii ? ">" : "▸"}) | fg(th.accent, ui_);
        }
        map = vbox({text(header) | fg(th.dim, ui_),
                    hbox({vbox(std::move(labels)) | size(WIDTH, EQUAL, 5),
                          heatmap(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(WIDTH, EQUAL, map_width) |
                              size(HEIGHT, EQUAL, map_height) | reflect(matrix_box_)})});
    }
    Elements extra;
    if (numbers_ && !fits_numbers)
        extra.push_back(text(fmt::format(fmt::runtime(tr("tui.matrix.numbers_off")), fmt::arg("n", max_numbers_n(width - info_width)))) | fg(th.warn, ui_));
    auto body = hbox({vbox({map, vbox(std::move(extra))}), text("   "), row_info(*d) | flex});
    return frame_panel(main, body, {{tr("tui.matrix.legend"), false}}, title);
}

ftxui::Element MatrixScreen::origin_panel(const MatrixData* d, int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements left{text(tr("tui.matrix.toeplitz")) | fg(th.dim, ui_)};
    if (d && d->toeplitz.size() > 0)
        for (Eigen::Index r = 0; r < d->toeplitz.rows(); ++r) {
            const bool keep = r % 2 == 1;
            Elements cells;
            for (Eigen::Index c = 0; c < d->toeplitz.cols(); ++c) {
                const double v = d->toeplitz(r, c);
                auto cell = text(pad_left(std::abs(v) < 1e-12 ? "·" : signed_fixed(v, 2), kNumberWidth)) |
                            fg(std::abs(v) < 1e-12 ? th.dim : keep ? th.trace[0] : th.dim, ui_);
                if (keep && std::abs(v) >= 1e-12) cell = cell | bg(tint(th.bg, th.trace[0], 0.15), ui_);
                cells.push_back(cell);
            }
            left.push_back(hbox(std::move(cells)));
        }
    const int matrix_width = d ? static_cast<int>(d->toeplitz.cols()) * kNumberWidth : 0;
    const bool stacked = width - 4 - matrix_width - 3 < 36;
    Elements right;
    for (const char* key : {"tui.matrix.origin1", "tui.matrix.origin2", "tui.matrix.origin3", "", "tui.matrix.origin4", "tui.matrix.origin5",
                            "tui.matrix.origin6", "", "tui.matrix.origin7", "tui.matrix.origin8"}) {
        const bool strong = std::string_view{key} == "tui.matrix.origin2" || std::string_view{key} == "tui.matrix.origin3";
        right.push_back(*key ? paragraph(tr(key)) | fg(strong ? th.fg : th.dim, ui_) : text(""));
    }
    auto body = stacked ? vbox({vbox(std::move(left)), text(""), vbox(std::move(right))})
                        : hbox({vbox(std::move(left)), text("   "), vbox(std::move(right)) | flex});
    return frame_panel(origin, body, {{tr("tui.matrix.origin_chip"), false}}, tr("tui.matrix.origin"));
}

ftxui::Element MatrixScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    clickables_.clear();
    matrix_box_ = kNoBox;
    sigma_zoom_.begin_frame();
    if (!ui_.jobs) return text("");
    if (!model_->data() && !model_->job()) apply(model_->options());
    if (animate_) {
        const auto t = now();
        if (t - last_step_ >= kAnimationStep) {
            last_step_ = t;
            ++row_;
            const auto d = model_->data();
            if (d && row_ >= static_cast<std::size_t>(d->m.rows())) row_ = 0;
        }
    }
    if (ui_.ticker) ui_.ticker->want("matrix", animate_);
    const auto d = model_->data();
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const int height = std::max(3, rows - 3);
    if (breakpoint_ <= Breakpoint::small || maximized_) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        main_width_ = cols;
        switch (p) {
        case controls: return controls_panel();
        case checks: return checks_panel(d.get());
        case main: return main_panel(d.get(), cols, height);
        case origin: return origin_panel(d.get(), cols);
        case panel_count: break;
        }
    }
    main_width_ = cols - kLeftWidth - 1;
    const int origin_rows = 15;
    const int main_rows = std::max(8, height - origin_rows - 1);
    auto left = vbox({controls_panel(), text(""), checks_panel(d.get()) | flex});
    auto right = vbox({main_panel(d.get(), main_width_, main_rows) | size(HEIGHT, EQUAL, main_rows), text(""), origin_panel(d.get(), main_width_) | flex});
    return hbox({left | size(WIDTH, EQUAL, kLeftWidth), text(" "), right | flex});
}

void MatrixScreen::show_cell() {
    const auto d = model_->data();
    if (!d || d->m.size() == 0) return;
    const auto r = std::min<std::size_t>(row_, static_cast<std::size_t>(d->m.rows() - 1));
    const auto c = std::min<std::size_t>(col_, static_cast<std::size_t>(d->m.cols() - 1));
    notice_ = fmt::format("A[{}, {}] = {}", r, c, signed_fixed(d->m(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)), 4));
}

bool MatrixScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (!ui_.jobs) return false;
    const auto d = model_->data();
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        using ftxui::Mouse;
        if (spectrum_ && sigma_zoom_.on_mouse(m)) return true;
        if (spectrum_ && m.button == Mouse::Left && m.motion == Mouse::Pressed) sigma_zoom_.press(m.x, m.y);
        if ((m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) && d && d->m.size() > 0 && matrix_box_.Contain(m.x, m.y)) {
            const double f = m.button == Mouse::WheelUp ? kWheelStep : 1 / kWheelStep;
            const auto [fr, fc] = fractions_at(m.x, m.y);
            row_zoom_.zoom(f, fr);
            col_zoom_.zoom(f, fc);
            return true;
        }
        if (drag_ && m.motion != Mouse::Pressed) {
            if (m.motion == Mouse::Released || m.button != Mouse::Left) {
                drag_ = false;
                return m.motion == Mouse::Released;
            }
            const auto [r0, c0] = fractions_at(drag_x_, drag_y_);
            const auto [r1, c1] = fractions_at(m.x, m.y);
            row_zoom_.pan(r0 - r1);
            col_zoom_.pan(c0 - c1);
            drag_x_ = m.x, drag_y_ = m.y;
            return true;
        }
        drag_ = false;
        if (m.button != ftxui::Mouse::Left || m.motion != ftxui::Mouse::Pressed) return false;
        for (auto& c : clickables_)
            if (c.box.Contain(m.x, m.y)) {
                auto action = c.action;
                action();
                return true;
            }
        if (d && matrix_box_.Contain(m.x, m.y) && d->m.size() > 0) {
            const auto rows = static_cast<std::size_t>(d->m.rows()), cols = static_cast<std::size_t>(d->m.cols());
            const int w = matrix_box_.x_max - matrix_box_.x_min + 1, h = matrix_box_.y_max - matrix_box_.y_min + 1;
            std::size_t c = 0, r = 0;
            if (numbers_) {
                c = block_.c0 + static_cast<std::size_t>(std::max(0, (m.x - matrix_box_.x_min - 5) / kNumberWidth));
                r = block_.r0 + static_cast<std::size_t>(m.y - matrix_box_.y_min);
            } else {
                c = block_.c0 + static_cast<std::size_t>(m.x - matrix_box_.x_min) * block_.cols / static_cast<std::size_t>(w);
                r = block_.r0 + static_cast<std::size_t>(2 * (m.y - matrix_box_.y_min)) * block_.rows / static_cast<std::size_t>(2 * h);
            }
            r = std::min(r, rows - 1);
            c = std::min(c, cols - 1);
            row_ = r;
            col_ = c;
            drag_ = true;
            drag_x_ = m.x, drag_y_ = m.y;
            notice_ = fmt::format("A[{}, {}] = {}", r, c, signed_fixed(d->m(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)), 4));
            return true;
        }
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) {
                focus_ = static_cast<Panel>(p);
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
    const auto rows = d ? static_cast<std::size_t>(d->m.rows()) : 0, cols = d ? static_cast<std::size_t>(d->m.cols()) : 0;
    const auto move_row = [&](std::size_t r) {
        if (rows == 0) return true;
        row_ = std::min(r, rows - 1);
        row_zoom_.show((static_cast<double>(row_) + 0.5) / static_cast<double>(rows));
        show_cell();
        return true;
    };
    if (e == Event::ArrowDown) return move_row(row_ + 1);
    if (e == Event::ArrowUp) return move_row(row_ > 0 ? row_ - 1 : 0);
    if (e == Event::PageDown) return move_row(row_ + 8);
    if (e == Event::PageUp) return move_row(row_ > 8 ? row_ - 8 : 0);
    if (e == Event::Home) return move_row(0);
    if (e == Event::End) return move_row(rows > 0 ? rows - 1 : 0);
    if ((e == Event::ArrowRight || e == Event::ArrowLeft) && cols > 0) {
        col_ = e == Event::ArrowRight ? std::min(col_ + 1, cols - 1) : (col_ > 0 ? col_ - 1 : 0);
        col_zoom_.show((static_cast<double>(col_) + 0.5) / static_cast<double>(cols));
        show_cell();
        return true;
    }
    if (spectrum_ && sigma_zoom_.on_key(e, "sigma")) return true;
    if (e == Event::Character('+') || e == Event::Character('=') || e == Event::Character('-')) {
        const double f = e == Event::Character('-') ? 1 / kWheelStep : kWheelStep;
        row_zoom_.zoom(f, rows ? (static_cast<double>(row_) + 0.5) / static_cast<double>(rows) : 0.5);
        col_zoom_.zoom(f, cols ? (static_cast<double>(col_) + 0.5) / static_cast<double>(cols) : 0.5);
        return true;
    }
    if (e == Event::Character('>') || e == Event::Character('.')) return col_zoom_.pan_view(0.2), true;
    if (e == Event::Character('<') || e == Event::Character(',')) return col_zoom_.pan_view(-0.2), true;
    if (e == Event::Character('0')) {
        row_zoom_.reset();
        col_zoom_.reset();
        return true;
    }
    if (e == Event::Character('v') || e == Event::Character('V')) {
        constexpr std::array ops{MatrixOp::analysis,  MatrixOp::multilevel,        MatrixOp::convolution,
                                 MatrixOp::circulant, MatrixOp::synthesis_product, MatrixOp::toeplitz_hankel};
        auto k = model_->options();
        const auto it = std::find(ops.begin(), ops.end(), k.op);
        const auto i = static_cast<std::size_t>(it - ops.begin());
        k.op = ops[e == Event::Character('v') ? (i + 1) % ops.size() : (i + ops.size() - 1) % ops.size()];
        apply(k);
        return true;
    }
    if (e == Event::Character(' ')) {
        animate_ = !animate_;
        last_step_ = now();
        return true;
    }
    if (e == Event::Character('c')) return numbers_ = !numbers_, true;
    if (e == Event::Character('s')) return spectrum_ = !spectrum_, true;
    if (e == Event::Character('t')) {
        auto k = model_->options();
        k.gram = !k.gram;
        apply(k);
        return true;
    }
    if (e == Event::Character('b')) {
        auto k = model_->options();
        auto it = std::find(kModes.begin(), kModes.end(), k.mode);
        k.mode = it == kModes.end() || it + 1 == kModes.end() ? kModes.front() : *(it + 1);
        apply(k);
        return true;
    }
    if (e == Event::Character('o')) {
        auto k = model_->options();
        k.order = k.order == RowOrder::blocks ? RowOrder::interleaved : RowOrder::blocks;
        apply(k);
        return true;
    }
    if (e == Event::Character('n')) {
        auto k = model_->options();
        k.n = std::min<std::size_t>(k.n + 2, 1024);
        apply(k);
        return true;
    }
    if (e == Event::Character('N')) {
        auto k = model_->options();
        k.n = k.n > 2 ? k.n - 2 : 2;
        apply(k);
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

void MatrixScreen::open_with(const NavArgs& args) {
    auto k = model_->options();
    if (!args.wavelet.empty()) k.wavelet = args.wavelet;
    if (args.n) k.n = *args.n;
    row_ = 0;
    apply(k);
}

ftxui::Box MatrixScreen::area(std::string_view id) const {
    if (id == "sigma_plot") return sigma_zoom_.box("sigma");
    if (id == "matrix") return matrix_box_;
    for (const auto& c : clickables_)
        if (c.id == id) return c.box;
    return kNoBox;
}

std::vector<JumpTarget> MatrixScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
}

std::vector<Command> MatrixScreen::commands() {
    std::vector<Command> out;
    for (auto name : core::wavelet_names())
        out.push_back({"matrix.filter." + std::string{name}, tr("tui.matrix.cmd.filter") + " " + std::string{name}, [this, n = std::string{name}] {
                           auto k = model_->options();
                           k.wavelet = n;
                           apply(k);
                       }});
    return out;
}

void MatrixScreen::update_block(std::size_t rows, std::size_t cols) {
    auto range = [](AxisZoom& z, std::size_t n, std::size_t& start, std::size_t& count) {
        z.set_full({0, static_cast<double>(n)});
        const auto a = z.axis();
        const std::size_t least = std::min<std::size_t>(n, 2);
        std::size_t s = std::min(n - 1, static_cast<std::size_t>(std::max(0.0, std::floor(a.lo + 1e-9))));
        std::size_t e = std::min(n, static_cast<std::size_t>(std::max(0.0, std::ceil(a.hi - 1e-9))));
        if (e < s + least) {
            e = std::min(n, s + least);
            s = e - least;
        }
        start = s;
        count = e - s;
    };
    range(row_zoom_, rows, block_.r0, block_.rows);
    range(col_zoom_, cols, block_.c0, block_.cols);
}

std::pair<double, double> MatrixScreen::fractions_at(int x, int y) const {
    const auto d = model_->data();
    if (!d || d->m.size() == 0 || block_.rows == 0 || block_.cols == 0) return {0.5, 0.5};
    const double rows = static_cast<double>(d->m.rows()), cols = static_cast<double>(d->m.cols());
    const double w = matrix_box_.x_max - matrix_box_.x_min + 1, h = matrix_box_.y_max - matrix_box_.y_min + 1;
    double fx = 0, fy = 0;
    if (numbers_) {
        fx = (x - matrix_box_.x_min - 5 + 0.5) / (kNumberWidth * static_cast<double>(block_.cols));
        fy = (y - matrix_box_.y_min + 0.5) / static_cast<double>(block_.rows);
    } else {
        fx = (x - matrix_box_.x_min + 0.5) / std::max(1.0, w);
        fy = (y - matrix_box_.y_min + 0.5) / std::max(1.0, h);
    }
    fx = std::clamp(fx, 0.0, 1.0), fy = std::clamp(fy, 0.0, 1.0);
    return {(static_cast<double>(block_.r0) + fy * static_cast<double>(block_.rows)) / rows,
            (static_cast<double>(block_.c0) + fx * static_cast<double>(block_.cols)) / cols};
}

std::vector<MenuItem> MatrixScreen::context_menu(int x, int y) {
    if (spectrum_)
        if (auto reset = sigma_zoom_.reset_item(x, y)) return {*reset};
    if (!matrix_box_.Contain(x, y) || !(row_zoom_.zoomed() || col_zoom_.zoomed())) return {};
    return {{tr("tui.menu.reset_zoom"), "", [this] {
                 row_zoom_.reset();
                 col_zoom_.reset();
             }}};
}

std::optional<std::pair<int, int>> MatrixScreen::menu_anchor() const {
    const auto& b = panel_boxes_[static_cast<std::size_t>(focus_)];
    if (b.x_max < b.x_min) return std::nullopt;
    return std::pair{(b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2};
}

std::shared_ptr<TabScreen> make_matrix_screen(UiContext& ui, std::shared_ptr<MatrixModel> model) {
    return std::make_shared<MatrixScreen>(ui, std::move(model));
}

}  // namespace signals::tui
