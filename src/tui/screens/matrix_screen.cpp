#include "tui/screens/matrix_screen.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "core/design/families.hpp"
#include "i18n/i18n.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/heatmap.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kLeftWidth = 32;
constexpr int kNumberWidth = 6;
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

std::vector<Hint> MatrixScreen::hints() const {
    return {{"↑↓", tr("tui.matrix.hint.row")},      {"Space", tr("tui.matrix.hint.animate")}, {"c", tr("tui.matrix.hint.numbers")},
            {"t", "AᵀA"},                           {"b", tr("tui.matrix.hint.edges")},       {"o", tr("tui.matrix.hint.order")},
            {"+/−", "N"},                           {tr("tui.signal.key.click"), tr("tui.matrix.hint.cell")}};
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
                                                 {MatrixOp::synthesis_product, "product"}};
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
    double total = 0;
    for (std::size_t i = 0; i < terms.size(); ++i) {
        total += terms[i].product;
        if (i < 8)
            out.push_back(text("  " + signed_fixed(terms[i].coef, 4) + " · x" + subscript_count(terms[i].column) + " (" + format_number(terms[i].x) + ")  = " +
                               signed_fixed(terms[i].product, 3)) |
                          fg(th.fg, ui_));
    }
    if (terms.size() > 8) out.push_back(text(fmt::format(fmt::runtime(tr("tui.matrix.more_terms")), fmt::arg("n", terms.size() - 8))) | fg(th.dim, ui_));
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
    std::string name = "A = [L; B]";
    switch (d->options.op) {
    case MatrixOp::analysis: name = d->options.order == RowOrder::blocks ? "A = [L; B]" : tr("tui.matrix.name.interleaved"); break;
    case MatrixOp::multilevel: name = "W"; break;
    case MatrixOp::convolution: name = "T(h₀)"; break;
    case MatrixOp::circulant: name = "C(h₀)"; break;
    case MatrixOp::synthesis_product: name = "S·A"; break;
    }
    if (d->options.gram) name = "AᵀA";
    const auto title = fmt::format(fmt::runtime(tr("tui.matrix.title")), fmt::arg("name", name), fmt::arg("rows", rows), fmt::arg("cols", cols),
                                   fmt::arg("row", row_));
    const int info_width = 44;
    const int map_width = std::max(8, std::min(static_cast<int>(2 * cols), width - info_width - 10));
    const bool fits_numbers = static_cast<int>(cols) <= max_numbers_n(width - info_width);
    Element map;
    if (numbers_ && fits_numbers) {
        Elements lines;
        for (std::size_t r = 0; r < rows && static_cast<int>(lines.size()) < height - 4; ++r) {
            Elements cells{text(pad_right(r < d->row_names.size() ? d->row_names[r] : std::string{}, 5)) | fg(th.dim, ui_)};
            for (std::size_t c = 0; c < cols; ++c) {
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
        spec.rows = rows;
        spec.cols = cols;
        spec.scale = HeatScale::diverging;
        spec.highlight_row = row_;
        spec.values.reserve(rows * cols);
        for (std::size_t r = 0; r < rows; ++r)
            for (std::size_t c = 0; c < cols; ++c) spec.values.push_back(d->m(static_cast<Eigen::Index>(r), static_cast<Eigen::Index>(c)));
        const int map_height = std::max(2, std::min(static_cast<int>((rows + 1) / 2), height - 4));
        std::string header = "     ";
        for (std::size_t c = 0; c < cols && static_cast<int>(header.size()) < map_width + 5; c += 8) {
            const auto label = "x" + subscript_count(c);
            header += pad_right(label, std::min(16, map_width));
        }
        Elements labels(static_cast<std::size_t>(map_height), text(""));
        auto put_label = [&](std::size_t r) {
            const auto line = static_cast<std::size_t>(r * static_cast<std::size_t>(map_height) / std::max<std::size_t>(1, rows));
            if (line < labels.size() && r < d->row_names.size()) labels[line] = text(d->row_names[r]) | fg(th.dim, ui_);
        };
        put_label(0);
        if (d->detail_start > 0 && d->detail_start < rows) {
            put_label(d->detail_start - 1);
            put_label(d->detail_start);
        }
        put_label(rows - 1);
        const auto marker = static_cast<std::size_t>(row_ * static_cast<std::size_t>(map_height) / std::max<std::size_t>(1, rows));
        if (marker < labels.size()) labels[marker] = text(std::string{ui_.caps.glyphs == GlyphMode::ascii ? ">" : "▸"}) | fg(th.accent, ui_);
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

ftxui::Element MatrixScreen::origin_panel(const MatrixData* d) {
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
    Elements right;
    for (const char* key : {"tui.matrix.origin1", "tui.matrix.origin2", "tui.matrix.origin3", "", "tui.matrix.origin4", "tui.matrix.origin5",
                            "tui.matrix.origin6", "", "tui.matrix.origin7", "tui.matrix.origin8"})
        right.push_back(*key ? text(tr(key)) | fg(std::string_view{key} == "tui.matrix.origin2" || std::string_view{key} == "tui.matrix.origin3" ? th.fg : th.dim, ui_)
                             : text(""));
    return frame_panel(origin, hbox({vbox(std::move(left)), text("   "), vbox(std::move(right)) | flex}), {{tr("tui.matrix.origin_chip"), false}},
                       tr("tui.matrix.origin"));
}

ftxui::Element MatrixScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    clickables_.clear();
    matrix_box_ = kNoBox;
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
    if (breakpoint_ <= Breakpoint::small) {
        const auto p = static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        main_width_ = cols;
        switch (p) {
        case controls: return controls_panel();
        case checks: return checks_panel(d.get());
        case main: return main_panel(d.get(), cols, height);
        case origin: return origin_panel(d.get());
        case panel_count: break;
        }
    }
    main_width_ = cols - kLeftWidth - 1;
    const int origin_rows = 15;
    const int main_rows = std::max(8, height - origin_rows - 1);
    auto left = vbox({controls_panel(), text(""), checks_panel(d.get()) | flex});
    auto right = vbox({main_panel(d.get(), main_width_, main_rows) | size(HEIGHT, EQUAL, main_rows), text(""), origin_panel(d.get()) | flex});
    return hbox({left | size(WIDTH, EQUAL, kLeftWidth), text(" "), right | flex});
}

bool MatrixScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (!ui_.jobs) return false;
    const auto d = model_->data();
    if (e.is_mouse()) {
        const auto& m = e.mouse();
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
                c = static_cast<std::size_t>(std::max(0, (m.x - matrix_box_.x_min - 5) / kNumberWidth));
                r = static_cast<std::size_t>(m.y - matrix_box_.y_min);
            } else {
                c = static_cast<std::size_t>(m.x - matrix_box_.x_min) * cols / static_cast<std::size_t>(w);
                r = static_cast<std::size_t>(2 * (m.y - matrix_box_.y_min)) * rows / static_cast<std::size_t>(2 * h);
            }
            r = std::min(r, rows - 1);
            c = std::min(c, cols - 1);
            row_ = r;
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
    if (e == Event::ArrowDown) {
        if (d && row_ + 1 < static_cast<std::size_t>(d->m.rows())) ++row_;
        return true;
    }
    if (e == Event::ArrowUp) return row_ = row_ > 0 ? row_ - 1 : 0, true;
    if (e == Event::Character(' ')) {
        animate_ = !animate_;
        last_step_ = now();
        return true;
    }
    if (e == Event::Character('c')) return numbers_ = !numbers_, true;
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
    if (e == Event::Character('+') || e == Event::Character('=')) {
        auto k = model_->options();
        k.n += 2;
        apply(k);
        return true;
    }
    if (e == Event::Character('-')) {
        auto k = model_->options();
        k.n = k.n > 2 ? k.n - 2 : 2;
        apply(k);
        return true;
    }
    if (e == Event::Tab) {
        if (breakpoint_ <= Breakpoint::small) layout_.next_panel(panel_count);
        else focus_ = static_cast<Panel>((focus_ + 1) % panel_count);
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

std::shared_ptr<TabScreen> make_matrix_screen(UiContext& ui, std::shared_ptr<MatrixModel> model) {
    return std::make_shared<MatrixScreen>(ui, std::move(model));
}

}  // namespace signals::tui
