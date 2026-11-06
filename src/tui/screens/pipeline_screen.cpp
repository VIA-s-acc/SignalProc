#include "tui/screens/pipeline_screen.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/sig.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kRightWidth = 44, kTextHeight = 10, kRunHeight = 14;

pipeline::PortRef short_ref(const std::string& node, const std::string& port, std::size_t ports) { return {node, ports == 1 ? std::string{} : port, 0, 0}; }

}  // namespace

PipelineScreen::PipelineScreen(UiContext& ui, std::shared_ptr<PipelineModel> model) : ui_{ui}, model_{std::move(model)} { panel_boxes_.fill(kNoBox); }

std::string PipelineScreen::tab_label() const { return tr("tui.tab.pipeline"); }

std::vector<Hint> PipelineScreen::hints() const {
    return {{"Tab", tr("tui.explain.hint.tab")},       {"z", tr("tui.signal.hint.zoom")},  {"←→↑↓", tr("tui.pipeline.hint.walk")},
            {"e", tr("tui.pipeline.hint.wires")},       {"L", tr("tui.pipeline.hint.layout")}, {tr("tui.pipeline.key.drag"), tr("tui.pipeline.hint.connect")}};
}

ftxui::Element PipelineScreen::frame_panel(Panel p, ftxui::Element body, std::vector<Chip> chips, std::string title) {
    if (breakpoint_ <= Breakpoint::small)
        chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", static_cast<int>(p) + 1), fmt::arg("count", static_cast<int>(panel_count))),
                         false});
    return panel(title, std::move(chips), std::move(body), focus_ == p, ui_) | ftxui::reflect(panel_boxes_[static_cast<std::size_t>(p)]);
}

ftxui::Element PipelineScreen::toolbar() const {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto name = model_->path() ? model_->path()->filename().string() : tr("tui.pipeline.untitled");
    Elements parts{text(" " + name + " ") | bold | fg(th.fg, ui_)};
    parts.push_back(text(model_->dirty() ? "● " + tr("tui.pipeline.changed") : tr("tui.pipeline.saved")) | fg(model_->dirty() ? th.warn : th.dim, ui_));
    if (const auto p = model_->problem()) {
        const auto where = p->line > 0 ? fmt::format(fmt::runtime(tr("tui.pipeline.problem_line")), fmt::arg("line", p->line)) + " " : std::string{};
        parts.push_back(text("  " + where + p->message) | fg(th.warn, ui_));
    }
    if (!notice_.empty()) parts.push_back(text("  " + notice_) | fg(th.err, ui_));
    return hbox(std::move(parts));
}

void PipelineScreen::clamp_view() {
    ox_ = std::clamp(ox_, 0, std::max(0, picture_.width - view_w_));
    oy_ = std::clamp(oy_, 0, std::max(0, picture_.height - view_h_));
}

void PipelineScreen::follow_selection() {
    const auto& sel = model_->selection;
    if (sel.kind != Selection::Kind::node) return;
    for (const auto& n : picture_.nodes) {
        if (n.id != sel.node) continue;
        if (n.x < ox_) ox_ = n.x - 1;
        if (n.x + n.w > ox_ + view_w_) ox_ = n.x + n.w - view_w_ + 1;
        if (n.y < oy_) oy_ = n.y;
        if (n.y + n.h > oy_ + view_h_) oy_ = n.y + n.h - view_h_;
    }
    clamp_view();
}

ftxui::Element PipelineScreen::graph_panel(int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto* g = model_->graph();
    view_w_ = std::max(10, width);
    view_h_ = std::max(3, height - 1);
    picture_ = g ? draw_pipeline(*g, model_->doc(), model_->run_view(), model_->selection, ui_.caps.glyphs) : GraphPicture{};
    if (follow_) follow_selection();
    follow_ = false;
    clamp_view();
    if (picture_.nodes.empty())
        return frame_panel(kGraph, vbox({filler(), text(tr("tui.pipeline.empty")) | center | fg(th.dim, ui_), filler()}) | reflect(graph_box_), {},
                           tr("tui.pipeline.graph"));
    const auto color = [&](CellStyle s) {
        switch (s) {
        case CellStyle::dim: return th.dim;
        case CellStyle::accent:
        case CellStyle::selected: return th.accent;
        case CellStyle::ok: return th.ok;
        case CellStyle::warn: return th.warn;
        case CellStyle::err: return th.err;
        default: return th.fg;
        }
    };
    Elements rows;
    for (int y = oy_; y < oy_ + view_h_; ++y) {
        Elements segments;
        std::string run;
        auto style = CellStyle::plain;
        const auto flush = [&] {
            if (run.empty()) return;
            auto e = text(run) | fg(color(style), ui_);
            if (style == CellStyle::selected) e = e | bold;
            segments.push_back(std::move(e));
            run.clear();
        };
        for (int x = ox_; x < ox_ + view_w_; ++x) {
            const bool inside = x < picture_.width && y < picture_.height;
            const auto s = inside ? picture_.style(x, y) : CellStyle::plain;
            if (s != style) {
                flush();
                style = s;
            }
            run += inside ? picture_.glyph(x, y) : " ";
        }
        flush();
        rows.push_back(hbox(std::move(segments)));
    }
    int left = 0, right = 0, above = 0, below = 0;
    for (const auto& n : picture_.nodes) {
        left += n.x + n.w <= ox_;
        right += n.x >= ox_ + view_w_;
        above += n.y + n.h <= oy_;
        below += n.y >= oy_ + view_h_;
    }
    std::string more;
    const auto add = [&](int count, const char* arrow) {
        if (count > 0) more += (more.empty() ? "" : "   ") + std::string{arrow} + " " + fmt::format(fmt::runtime(tr("tui.theory.blocks_more")), fmt::arg("count", count));
    };
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    add(left, ascii ? "<" : "◂");
    add(right, ascii ? ">" : "▸");
    add(above, ascii ? "^" : "▴");
    add(below, ascii ? "v" : "▾");
    auto body = vbox({vbox(std::move(rows)) | reflect(graph_box_), text(more) | fg(th.dim, ui_)});
    return frame_panel(kGraph, std::move(body), {}, tr("tui.pipeline.graph"));
}

ftxui::Element PipelineScreen::inspector_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& sel = model_->selection;
    const auto* g = model_->graph();
    Elements rows;
    if (sel.kind == Selection::Kind::node && g) {
        const auto it = std::find_if(g->nodes.begin(), g->nodes.end(), [&](const auto& n) { return n.id == sel.node; });
        if (it != g->nodes.end()) {
            rows.push_back(text(it->id + " · " + block_name(it->type->id)) | bold | fg(th.fg, ui_));
            for (const auto& spec : it->type->params) {
                const auto value = it->params.count(spec.name) ? it->params.at(spec.name) : spec.fallback;
                rows.push_back(hbox({text(pad_right(spec.name, 14)) | fg(th.dim, ui_), text(value) | fg(th.fg, ui_)}));
            }
            const auto run = model_->run_view();
            if (const auto r = run.nodes.find(it->id); r != run.nodes.end() && !r->second.status.message.empty())
                rows.push_back(paragraph(r->second.status.message) | fg(th.err, ui_));
        }
    } else if (sel.kind == Selection::Kind::wire) {
        const auto ref = [](const pipeline::PortRef& r) { return r.port.empty() ? r.node : r.node + "." + r.port; };
        rows.push_back(text(ref(sel.wire.from) + " → " + ref(sel.wire.to)) | bold | fg(th.fg, ui_));
    } else {
        rows.push_back(paragraph(tr("tui.pipeline.inspector_hint")) | fg(th.dim, ui_));
    }
    return frame_panel(kInspector, vbox(std::move(rows)) | yframe, {}, tr("tui.pipeline.inspector"));
}

ftxui::Element PipelineScreen::run_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto run = model_->run_view();
    Elements rows;
    if (run.number == 0) rows.push_back(paragraph(tr("tui.pipeline.run_hint")) | fg(th.dim, ui_));
    else {
        auto title = fmt::format(fmt::runtime(tr("tui.pipeline.run_number")), fmt::arg("n", run.number));
        if (run.running) title += " · " + tr("tui.pipeline.running");
        else if (run.cancelled) title += " · " + tr("tui.pipeline.cancelled");
        else if (run.error) title += " · " + tr("tui.pipeline.failed");
        else title += " · " + format_si(run.seconds, tr("unit.s"));
        rows.push_back(text(title) | fg(run.error ? th.err : th.fg, ui_));
    }
    return frame_panel(kRun, vbox(std::move(rows)), {}, tr("tui.pipeline.run"));
}

ftxui::Element PipelineScreen::text_panel(int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements rows;
    std::istringstream in{pipeline::write_sig(model_->doc())};
    const auto& sel = model_->selection;
    for (std::string line; std::getline(in, line);) {
        const bool chosen = sel.kind == Selection::Kind::node && line.starts_with("node " + sel.node + " ");
        auto e = text(line) | fg(chosen ? th.accent : th.dim, ui_);
        rows.push_back(chosen ? e | bold | focus : e);
    }
    (void)height;
    return frame_panel(kText, vbox(std::move(rows)) | yframe, {}, tr("tui.pipeline.text"));
}

ftxui::Element PipelineScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    graph_box_ = kNoBox;
    picture_ = {};
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const int content = std::max(8, rows - 3);
    if (breakpoint_ <= Breakpoint::small || maximized_) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case kGraph: return vbox({toolbar(), graph_panel(cols - 4, content - 3) | flex});
        case kInspector: return vbox({toolbar(), inspector_panel() | flex});
        case kRun: return vbox({toolbar(), run_panel() | flex});
        case kText: return vbox({toolbar(), text_panel(content - 3) | flex});
        case panel_count: break;
        }
    }
    const int graph_w = cols - kRightWidth - 1 - 4, graph_h = content - 1 - kTextHeight - 2;
    auto left = vbox({graph_panel(graph_w, graph_h) | flex, text_panel(kTextHeight - 2) | size(HEIGHT, EQUAL, kTextHeight)});
    auto right = vbox({inspector_panel() | flex, run_panel() | size(HEIGHT, EQUAL, kRunHeight)});
    return vbox({toolbar(), hbox({left | flex, text(" "), right | size(WIDTH, EQUAL, kRightWidth)}) | flex});
}

std::optional<std::pair<int, int>> PipelineScreen::to_picture(int x, int y) const {
    if (!valid(graph_box_) || !graph_box_.Contain(x, y)) return std::nullopt;
    return std::pair{x - graph_box_.x_min + ox_, y - graph_box_.y_min + oy_};
}

ftxui::Box PipelineScreen::area(std::string_view id) const {
    const auto at = [&](int px, int py) {
        const int x = graph_box_.x_min + px - ox_, y = graph_box_.y_min + py - oy_;
        return ftxui::Box{x, x, y, y};
    };
    if (id == "graph") return graph_box_;
    for (const auto& n : picture_.nodes)
        if (id == "node:" + n.id) return ftxui::Box{graph_box_.x_min + n.x - ox_, graph_box_.x_min + n.x - ox_ + n.w - 1, graph_box_.y_min + n.y - oy_, graph_box_.y_min + n.y - oy_ + n.h - 1};
    for (const auto& p : picture_.ports)
        if (id == (p.output ? "out:" : "in:") + p.node + "." + p.port) return at(p.x, p.y);
    return kNoBox;
}

void PipelineScreen::move_selection(int dx, int dy) {
    const auto& nodes = picture_.nodes;
    if (nodes.empty()) return;
    auto& sel = model_->selection;
    const auto current = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return sel.kind == Selection::Kind::node && n.id == sel.node; });
    if (current == nodes.end()) {
        const auto first = std::min_element(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return std::pair{a.x, a.y} < std::pair{b.x, b.y}; });
        sel = {Selection::Kind::node, first->id, {}};
        follow_ = true;
        return;
    }
    const NodeHit* best = nullptr;
    long best_score = std::numeric_limits<long>::max();
    for (const auto& n : nodes) {
        const bool ok = dx > 0 ? n.x > current->x : dx < 0 ? n.x < current->x : n.x == current->x && (dy > 0 ? n.y > current->y : n.y < current->y);
        if (!ok) continue;
        const long score = dx != 0 ? 1000L * std::abs(n.x - current->x) + std::abs(n.y - current->y) : std::abs(n.y - current->y);
        if (score < best_score) best_score = score, best = &n;
    }
    if (best) sel = {Selection::Kind::node, best->id, {}};
    follow_ = true;
}

void PipelineScreen::cycle_wires() {
    auto& sel = model_->selection;
    if (sel.kind == Selection::Kind::node) anchor_ = sel.node;
    if (anchor_.empty()) return;
    std::vector<const WireHit*> touching;
    for (const auto& w : picture_.wires)
        if (w.edge.from.node == anchor_ || w.edge.to.node == anchor_) touching.push_back(&w);
    if (touching.empty()) return;
    std::size_t next = 0;
    if (sel.kind == Selection::Kind::wire)
        for (std::size_t k = 0; k < touching.size(); ++k)
            if (same_wire(touching[k]->edge, sel.wire)) next = (k + 1) % touching.size();
    sel = {Selection::Kind::wire, {}, touching[next]->edge};
}

bool PipelineScreen::on_mouse(const ftxui::Mouse& m) {
    using ftxui::Mouse;
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) focus_ = static_cast<Panel>(p);
        const auto at = to_picture(m.x, m.y);
        if (!at) return false;
        const auto [px, py] = *at;
        for (const auto& p : picture_.ports)
            if (p.x == px && p.y == py && p.output) {
                drag_ = {Drag::Kind::wire, p, m.x, m.y};
                return true;
            }
        for (const auto& n : picture_.nodes)
            if (px >= n.x && px < n.x + n.w && py >= n.y && py < n.y + n.h) {
                model_->selection = {Selection::Kind::node, n.id, {}};
                return true;
            }
        for (const auto& w : picture_.wires)
            if (std::find(w.cells.begin(), w.cells.end(), std::pair{px, py}) != w.cells.end()) {
                model_->selection = {Selection::Kind::wire, {}, w.edge};
                return true;
            }
        drag_ = {Drag::Kind::pan, {}, m.x, m.y};
        return true;
    }
    if (drag_.kind == Drag::Kind::pan && m.motion == Mouse::Moved) {
        ox_ -= m.x - drag_.last_x;
        oy_ -= m.y - drag_.last_y;
        drag_.last_x = m.x;
        drag_.last_y = m.y;
        clamp_view();
        return true;
    }
    if (m.motion == Mouse::Released && drag_.kind != Drag::Kind::none) {
        const auto kind = drag_.kind;
        const auto from = drag_.from;
        drag_ = {};
        if (kind != Drag::Kind::wire) return true;
        const auto at = to_picture(m.x, m.y);
        if (!at) return true;
        const auto* g = model_->graph();
        for (const auto& p : picture_.ports) {
            if (p.output || p.x != at->first || p.y != at->second || !g) continue;
            const auto count = [&](const std::string& id, bool outputs) {
                const auto it = std::find_if(g->nodes.begin(), g->nodes.end(), [&](const auto& n) { return n.id == id; });
                return it == g->nodes.end() ? std::size_t{0} : outputs ? it->type->outputs.size() : it->type->inputs.size();
            };
            const auto ok = model_->connect(short_ref(from.node, from.port, count(from.node, true)), short_ref(p.node, p.port, count(p.node, false)));
            notice_ = ok ? std::string{} : ok.error().message;
        }
        return true;
    }
    return drag_.kind != Drag::Kind::none;
}

bool PipelineScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (e.is_mouse()) return on_mouse(e.mouse());
    if (e == Event::Tab || e == Event::TabReverse) {
        const bool back = e == Event::TabReverse;
        const std::size_t step = back ? panel_count - 1 : 1;
        if (maximized_) maximized_ = focus_ = static_cast<Panel>((*maximized_ + step) % panel_count);
        else if (breakpoint_ <= Breakpoint::small) layout_.next_panel(panel_count, back);
        else focus_ = static_cast<Panel>((focus_ + step) % panel_count);
        return true;
    }
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
        return true;
    }
    if (e == Event::Escape) {
        if (maximized_) maximized_.reset();
        else if (model_->selection.kind != Selection::Kind::none) model_->selection = {};
        else return false;
        notice_.clear();
        return true;
    }
    if (e == Event::ArrowRight) return move_selection(1, 0), true;
    if (e == Event::ArrowLeft) return move_selection(-1, 0), true;
    if (e == Event::ArrowDown) return move_selection(0, 1), true;
    if (e == Event::ArrowUp) return move_selection(0, -1), true;
    if (e == Event::Character('e')) return cycle_wires(), true;
    if (e == Event::Character('L')) {
        ox_ = oy_ = 0;
        follow_ = true;
        return true;
    }
    return false;
}

std::shared_ptr<TabScreen> make_pipeline_screen(UiContext& ui, std::shared_ptr<PipelineModel> model) {
    return std::make_shared<PipelineScreen>(ui, std::move(model));
}

}  // namespace signals::tui
