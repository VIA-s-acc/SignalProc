#include "tui/screens/pipeline_screen.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <thread>

#include <fmt/format.h>

#include "core/analysis.hpp"
#include "core/metrics.hpp"
#include "core/spectral.hpp"
#include "core/design/families.hpp"
#include "i18n/i18n.hpp"
#include "io/temp_file.hpp"
#include "io/wav.hpp"
#include "pipeline/examples.hpp"
#include "pipeline/block.hpp"
#include "pipeline/sig.hpp"
#include "theory/pipeline_cards.hpp"
#include "tui/shell/palette.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/list_window.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/scroll.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kRightWidth = 44, kBottomHeight = 12, kTextWidth = 46, kRunHeight = 16;
constexpr std::size_t kStemFrames = 64;

std::filesystem::path utf8_file(const std::string& text) { return expand_path(text); }

std::optional<std::string> value_note(const std::string& block, const std::string& param, const std::string& value) {
    const auto key = "pipeline.param." + block + "." + param + ".";
    if (auto note = tr_opt(key + value)) return note;
    if (block == "mra" && param == "component" && value.size() > 1 && value[0] == 'd') return tr_opt(key + "d");
    return std::nullopt;
}

std::string range_text(const pipeline::ParamSpec& spec) {
    std::string out = fmt::format(fmt::runtime(tr("tui.pipeline.param_default")), fmt::arg("value", spec.fallback.empty() ? "—" : spec.fallback));
    if (std::isfinite(spec.min) && std::isfinite(spec.max))
        out += " · " + fmt::format(fmt::runtime(tr("tui.pipeline.param_range")), fmt::arg("min", format_number(spec.min, 6)), fmt::arg("max", format_number(spec.max, 6)));
    return out;
}

pipeline::PortRef short_ref(const std::string& node, const std::string& port, std::size_t ports) { return {node, ports == 1 ? std::string{} : port, 0, 0}; }

}  // namespace

PipelineScreen::PipelineScreen(UiContext& ui, std::shared_ptr<PipelineModel> model, std::function<std::optional<std::filesystem::path>()> current_file)
    : ui_{ui}, model_{std::move(model)}, current_file_{std::move(current_file)} {
    panel_boxes_.fill(kNoBox);
}

TheoryLink pipeline_theory_link(std::string_view block, std::string title) {
    const auto target = theory::block_theory(block);
    return TheoryLink{target.section ? TheoryLink::Kind::section : TheoryLink::Kind::card, target.id, std::move(title)};
}

std::optional<TheoryLink> PipelineScreen::theory_link() const {
    const auto* n = selected_node();
    if (!n) return TheoryLink{TheoryLink::Kind::section, "sig-format", tr("tui.pipeline.cmd.format")};
    return pipeline_theory_link(n->type->id, block_name(n->type->id));
}

void PipelineScreen::publish(theory::LiveValues& v) const {
    const auto* n = selected_node();
    if (!n) return;
    v.source = n->id;
    for (const auto& [key, value] : n->params) v.set("block." + key, value);
    const auto run = model_->run_view();
    if (const auto it = run.nodes.find(n->id); it != run.nodes.end()) {
        v.set("block.wall", format_si(it->second.status.wall_seconds, tr("unit.s")));
        v.set("block.bytes", format_si(static_cast<double>(it->second.status.bytes), tr("unit.byte")));
    }
}

std::string PipelineScreen::context() const {
    return model_->path() ? model_->path()->filename().string() : tr("tui.pipeline.untitled");
}

void PipelineScreen::open_example(std::string_view name, std::string_view text) {
    if (auto ok = model_->load_text(name, text); !ok) return report(ok);
    std::vector<std::string> sources;
    std::string initial;
    for (const auto& n : model_->doc().nodes)
        if (n.type == "wav") {
            sources.push_back(n.id);
            if (initial.empty() && n.params.count("path")) initial = n.params.at("path");
        }
    if (sources.empty()) return;
    const auto set_path = [this, sources](const std::string& path) {
        for (const auto& id : sources) report(model_->set_param(id, "path", path));
    };
    if (const auto file = current_file_ ? current_file_() : std::nullopt) {
        initial = std::filesystem::absolute(*file).string();
        set_path(initial);
    }
    if (ui_.ask_text) ui_.ask_text(tr("tui.pipeline.ask_wav"), initial, [set_path](std::string path) { set_path(path); });
}

void PipelineScreen::open_with(const NavArgs& args) {
    if (args.example.empty()) return;
    for (const auto& ex : pipeline::examples()) {
        if (ex.name != args.example) continue;
        open_example(ex.name, ex.text);
        if (ui_.jobs) model_->run(*ui_.jobs);
        return;
    }
}

void PipelineScreen::open_examples() {
    std::vector<Command> items;
    for (const auto& ex : pipeline::examples()) {
        const auto key = "tui.pipeline.example." + std::string{ex.name};
        const auto title = tr(key);
        items.push_back({std::string{ex.name}, title == key ? std::string{ex.name} : title, [this, ex] { open_example(ex.name, ex.text); }});
    }
    if (ui_.choose) ui_.choose(tr("tui.pipeline.cmd.example"), std::move(items));
    else if (!items.empty()) items.front().run();
}

std::vector<Command> PipelineScreen::commands() {
    std::vector<Command> out;
    out.push_back({"pipeline.example", tr("tui.pipeline.cmd.example"), [this] { open_examples(); }});
    out.push_back({"pipeline.format", tr("tui.pipeline.cmd.format"), [this] {
                       if (ui_.open_theory) ui_.open_theory(TheoryPage{TheoryLink{TheoryLink::Kind::section, "sig-format", tr("tui.pipeline.cmd.format")}});
                   }});
    out.push_back({"pipeline.run", tr("tui.pipeline.cmd.run"), [this] {
                       if (ui_.jobs) model_->run(*ui_.jobs);
                   }});
    out.push_back({"pipeline.save", tr("tui.pipeline.cmd.save"), [this] { OnEvent(ftxui::Event::CtrlS); }});
    out.push_back({"pipeline.open", tr("tui.pipeline.cmd.open"), [this] { OnEvent(ftxui::Event::Character('o')); }});
    out.push_back({"pipeline.probe_name", tr("tui.pipeline.cmd.probe_name"), [this] { name_probe(); }});
    out.push_back({"pipeline.sweep", tr("tui.pipeline.cmd.sweep"), [this] { start_sweep(); }});
    out.push_back({"pipeline.duplicate", tr("tui.pipeline.cmd.duplicate"), [this] { duplicate_selected(); }});
    out.push_back({"pipeline.probe_copy", tr("tui.pipeline.cmd.probe_copy"), [this] { copy_probe(); }});
    out.push_back({"pipeline.probe_compare", tr("tui.pipeline.cmd.compare"), [this] { toggle_compare(); }});
    out.push_back({"pipeline.probe_save", tr("tui.pipeline.cmd.probe_save"), [this] { save_probe(); }});
    return out;
}

std::string PipelineScreen::tab_label() const { return tr("tui.tab.pipeline"); }

std::vector<Hint> PipelineScreen::hints() const {
    if (adding_) return {{"↑↓", tr("tui.pipeline.hint.pick")}, {"Enter", tr("tui.pipeline.hint.insert")}, {"Esc", tr("tui.pipeline.hint.close")}};
    return {{"Tab", tr("tui.explain.hint.tab")},          {"z", tr("tui.signal.hint.zoom")},       {"a", tr("tui.pipeline.hint.add")},
            {"Enter", tr("tui.pipeline.hint.params")},     {"←→↑↓", tr("tui.pipeline.hint.walk")},  {"e", tr("tui.pipeline.hint.wires")},
            {"M", tr("tui.pipeline.hint.bypass")},         {"p", tr("tui.pipeline.hint.probe")},     {"Del", tr("tui.pipeline.hint.delete")},
            {"Ctrl+Z/Y", tr("tui.pipeline.hint.undo")},    {"Ctrl+S", tr("tui.pipeline.hint.save")}, {"o", tr("tui.pipeline.hint.open")},
            {"r", tr("tui.pipeline.hint.run")},           {"A/B", tr("tui.pipeline.hint.ab")},      {"Space", tr("tui.pipeline.hint.listen")},
            {"+ − 0", tr("tui.pipeline.hint.zoom")},       {"O", tr("tui.pipeline.hint.examples")},  {"PgUp/PgDn", tr("tui.hint.page")},
            {"v", tr("tui.pipeline.hint.view")},          {"n", tr("tui.pipeline.hint.name")},     {"k", tr("tui.pipeline.hint.channels")},     {"S", tr("tui.pipeline.hint.sweep")},     {"D", tr("tui.pipeline.hint.duplicate")},  {"y", tr("tui.pipeline.hint.copy_probe")},  {"x", tr("tui.pipeline.hint.save_probe")},  {"w", tr("tui.pipeline.hint.compare")},  {"+ − E", tr("tui.pipeline.hint.digits")},     {"L", tr("tui.pipeline.hint.layout")},   {tr("tui.pipeline.key.drag"), tr("tui.pipeline.hint.connect")},
            {"c/C", tr("tui.pipeline.hint.connect_keys")}};
}

const pipeline::GraphNode* PipelineScreen::selected_node() const {
    const auto* g = model_->graph();
    if (!g || model_->selection.kind != Selection::Kind::node) return nullptr;
    const auto it = std::find_if(g->nodes.begin(), g->nodes.end(), [&](const auto& n) { return n.id == model_->selection.node; });
    return it == g->nodes.end() ? nullptr : &*it;
}

void PipelineScreen::report(const core::Result<void>& r) { notice_ = r ? std::string{} : r.error().message; }

void PipelineScreen::file_result(const core::Result<void>& r) {
    report(r);
    if (r && ui_.session && model_->path()) ui_.session->remember(RecentKind::graph, *model_->path());
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
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const int cols = ui_.terminal_size ? ui_.terminal_size().first : 150;
    const auto name = ellipsize_start(model_->path() ? model_->path()->filename().string() : tr("tui.pipeline.untitled"), std::max(8, cols / 3), ascii);
    const auto state = model_->dirty() ? "● " + tr("tui.pipeline.changed") : tr("tui.pipeline.saved");
    Elements parts{text(" " + name + " ") | bold | fg(th.fg, ui_), text(state) | fg(model_->dirty() ? th.warn : th.dim, ui_)};
    int room = cols - display_width(name) - 2 - display_width(state);
    if (const auto p = model_->problem()) {
        const auto where = p->line > 0 ? fmt::format(fmt::runtime(tr("tui.pipeline.problem_line")), fmt::arg("line", p->line)) + " " : std::string{};
        const auto shown = ellipsize("  " + where + p->message, std::max(0, room), ascii);
        room -= display_width(shown);
        parts.push_back(text(shown) | fg(th.warn, ui_));
    }
    if (!notice_.empty()) parts.push_back(text(ellipsize("  " + notice_, std::max(0, room), ascii)) | fg(th.err, ui_));
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
    picture_ = g ? draw_pipeline(*g, model_->doc(), model_->run_view(), model_->selection, ui_.caps.glyphs, graph_zoom_) : GraphPicture{};
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
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    std::map<std::pair<int, int>, std::pair<std::string, CellStyle>> overlay;
    std::string wire_note;
    if (drag_.kind == Drag::Kind::wire) {
        std::optional<std::pair<int, int>> end;
        if (valid(drag_.box) && drag_.box.Contain(drag_.last_x, drag_.last_y))
            end = std::pair{drag_.last_x - drag_.box.x_min + ox_, drag_.last_y - drag_.box.y_min + oy_};
        const int fx = drag_.from.x, fy = drag_.from.y;
        const int tx = end ? end->first : fx, ty = end ? end->second : fy;
        const PortHit* target = nullptr;
        for (const auto& p : picture_.ports)
            if (!p.output && end && p.x == tx && p.y == ty) target = &p;
        const auto dot = std::string{ascii ? "." : "·"};
        for (int x = std::min(fx + 1, tx); x <= std::max(fx + 1, tx); ++x) overlay[{x, fy}] = {dot, CellStyle::accent};
        for (int y = std::min(fy, ty); y <= std::max(fy, ty); ++y) overlay[{tx, y}] = {dot, CellStyle::accent};
        if (target) overlay[{tx, ty}] = {picture_.glyph(tx, ty), CellStyle::ok};
        else if (end) overlay[{tx, ty}] = {ascii ? "*" : "●", CellStyle::accent};
        const auto name = [](const PortHit& p) { return p.port.empty() ? p.node : p.node + "." + p.port; };
        wire_note = fmt::format(fmt::runtime(tr("tui.pipeline.wire_drag")), fmt::arg("from", name(drag_.from)), fmt::arg("to", target ? name(*target) : std::string{"…"}));
    }
    const auto drawn = [&](int x, int y) { return x >= 0 && x < picture_.width && y >= 0 && y < picture_.height && picture_.glyph(x, y) != " "; };
    const std::string cut_mark = ascii ? "~" : "…";
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
            const auto over = overlay.find({x, y});
            const bool cut = over == overlay.end() && ((x == ox_ + view_w_ - 1 && drawn(x + 1, y)) || (x == ox_ && drawn(x - 1, y)));
            const auto s = cut ? CellStyle::dim : over != overlay.end() ? over->second.second : inside ? picture_.style(x, y) : CellStyle::plain;
            if (s != style) {
                flush();
                style = s;
            }
            run += cut ? cut_mark : over != overlay.end() ? over->second.first : inside ? picture_.glyph(x, y) : " ";
        }
        flush();
        rows.push_back(hbox(std::move(segments)));
    }
    int left = 0, right = 0, above = 0, below = 0;
    for (const auto& n : picture_.nodes) {
        left += n.x < ox_;
        right += n.x + n.w > ox_ + view_w_;
        above += n.y < oy_;
        below += n.y + n.h > oy_ + view_h_;
    }
    std::string more;
    const auto add = [&](int count, const char* arrow) {
        if (count > 0) more += (more.empty() ? "" : "   ") + std::string{arrow} + " " + fmt::format(fmt::runtime(tr("tui.theory.blocks_more")), fmt::arg("count", count));
    };
    add(left, ascii ? "<" : "◂");
    add(right, ascii ? ">" : "▸");
    add(above, ascii ? "^" : "▴");
    add(below, ascii ? "v" : "▾");
    auto body = vbox({vbox(std::move(rows)) | reflect(graph_box_), wire_note.empty() ? text(more) | fg(th.dim, ui_) : text(wire_note) | fg(th.accent, ui_)});
    return frame_panel(kGraph, std::move(body), {{fmt::format(fmt::runtime(tr("tui.pipeline.zoom_chip")), fmt::arg("level", tr("tui.pipeline.zoom." + std::to_string(graph_zoom_)))), false}}, tr("tui.pipeline.graph"));
}

ftxui::Element PipelineScreen::inspector_panel(int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& sel = model_->selection;
    const auto* g = model_->graph();
    Elements rows;
    if (sel.kind == Selection::Kind::node && g) {
        const auto it = std::find_if(g->nodes.begin(), g->nodes.end(), [&](const auto& n) { return n.id == sel.node; });
        if (it != g->nodes.end()) {
            rows.push_back(text(ellipsize(it->id + " · " + block_name(it->type->id), width, ui_.caps.glyphs == GlyphMode::ascii)) | bold | fg(th.fg, ui_));
            const auto view = model_->run_view();
            if (const auto r = view.nodes.find(it->id); r != view.nodes.end() && !r->second.status.shapes.empty())
                rows.push_back(text(ellipsize(tr("tui.pipeline.output") + ": " + shape_text(r->second.status.shapes, false), width, ui_.caps.glyphs == GlyphMode::ascii)) | fg(th.dim, ui_));
            param_ = std::min(param_, it->type->params.empty() ? 0 : it->type->params.size() - 1);
            Elements params;
            param_boxes_.assign(it->type->params.size(), kNoBox);
            for (std::size_t k = 0; k < it->type->params.size(); ++k) {
                const auto& spec = it->type->params[k];
                const auto value = it->params.count(spec.name) ? it->params.at(spec.name) : spec.fallback;
                const bool choice = spec.kind == pipeline::ParamSpec::Kind::choice;
                const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
                const int room = std::max(4, width - 14);
                const auto shown = choice ? "◂ " + ellipsize(value, room - 4, ascii) + " ▸" : spec.kind == pipeline::ParamSpec::Kind::path ? ellipsize_start(value, room, ascii) : ellipsize(value, room, ascii);
                auto row = hbox({text(pad_right(ellipsize(spec.name, 13, ascii), 14)) | fg(th.dim, ui_), text(shown) | fg(th.fg, ui_), filler()}) | reflect(param_boxes_[k]);
                if (focus_ == kInspector && k == param_) row = row | bg(tint(th.bg, th.accent, 0.3), ui_);
                params.push_back(std::move(row));
            }
            Elements tail;
            if (!it->type->params.empty()) {
                const auto& spec = it->type->params[param_];
                const auto value = it->params.count(spec.name) ? it->params.at(spec.name) : spec.fallback;
                tail.push_back(separator() | fg(th.dim, ui_));
                const auto about = tr_opt("pipeline.param." + it->type->id + "." + spec.name);
                for (const auto& line : wrap_words(spec.name + (about ? " — " + *about : std::string{}), std::max(8, width))) tail.push_back(text(line) | fg(th.fg, ui_));
                if (const auto note = value_note(it->type->id, spec.name, value))
                    for (const auto& line : wrap_words(value + ": " + *note, std::max(8, width))) tail.push_back(text(line) | fg(th.accent, ui_));
                for (const auto& line : wrap_words(range_text(spec), std::max(8, width))) tail.push_back(text(line) | fg(th.dim, ui_));
            }
            if (it->bypass) tail.push_back(text(tr("tui.pipeline.state.bypassed")) | fg(th.warn, ui_));
            const auto run = model_->run_view();
            if (const auto r = run.nodes.find(it->id); r != run.nodes.end() && !r->second.status.message.empty())
                for (const auto& line : wrap_words(r->second.status.message, std::max(8, width))) tail.push_back(text(line) | fg(th.err, ui_));
            const auto key = it->id + "#" + std::to_string(param_);
            const auto follow = key != param_follow_ ? std::optional{param_} : std::nullopt;
            param_follow_ = key;
            const int budget = std::max(2, height - 1 - static_cast<int>(tail.size()));
            rows.push_back(scrolled(std::move(params), inspector_scroll_, budget, follow, ui_));
            for (auto& t : tail) rows.push_back(std::move(t));
        }
    } else if (sel.kind == Selection::Kind::wire) {
        const auto ref = [](const pipeline::PortRef& r) { return r.port.empty() ? r.node : r.node + "." + r.port; };
        rows.push_back(text(ellipsize(ref(sel.wire.from) + " → " + ref(sel.wire.to), width, ui_.caps.glyphs == GlyphMode::ascii)) | bold | fg(th.fg, ui_));
    } else {
        for (const auto& line : wrap_words(tr("tui.pipeline.inspector_hint"), std::max(8, width))) rows.push_back(text(line) | fg(th.dim, ui_));
    }
    return frame_panel(kInspector, vbox(std::move(rows)), {}, tr("tui.pipeline.inspector"));
}

ftxui::Element PipelineScreen::add_panel(int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    std::vector<Command> all;
    for (const auto& t : pipeline::block_types()) all.push_back({t.id, block_name(t.id), {}});
    const auto found = filter_commands(all, query_);
    pick_ = std::min(pick_, found.empty() ? 0 : found.size() - 1);
    Elements rows{hbox({text("/ " + query_) | fg(th.fg, ui_), text(ui_.caps.glyphs == GlyphMode::ascii ? "_" : "▏") | fg(th.accent, ui_), filler(),
                        text(std::to_string(found.size()) + " / " + std::to_string(all.size())) | fg(th.dim, ui_)}),
                  text(model_->selection.kind == Selection::Kind::wire ? tr("tui.pipeline.add_into_wire") : tr("tui.pipeline.add_free")) | fg(th.dim, ui_)};
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    add_rows_.assign(found.size(), {std::string{}, kNoBox});
    Elements items;
    for (std::size_t k = 0; k < found.size(); ++k) {
        add_rows_[k].first = found[k]->id;
        const auto id_width = display_width(found[k]->id);
        items.push_back(hbox({text(ellipsize((k == pick_ ? "▸ " : "  ") + found[k]->title, std::max(6, width - id_width - 2), ascii)) | fg(k == pick_ ? th.accent : th.fg, ui_), filler(),
                              text(found[k]->id) | fg(th.dim, ui_)}) |
                        reflect(add_rows_[k].second));
    }
    Elements about;
    if (!found.empty())
        if (const auto* type = pipeline::find_block_type(found[std::min(pick_, found.size() - 1)]->id)) {
            if (const auto purpose = tr_opt("pipeline.about." + type->id))
                for (const auto& line : wrap_words(*purpose, width)) about.push_back(text(line) | fg(th.fg, ui_));
            auto ports = [](const std::vector<pipeline::PortSpec>& list) {
                std::string s;
                for (const auto& p : list) s += (s.empty() ? "" : ", ") + p.name + ": " + std::string{pipeline::to_string(p.type)};
                return s.empty() ? std::string{"—"} : s;
            };
            for (const auto& line : wrap_words(ports(type->inputs) + " → " + ports(type->outputs), width)) about.push_back(text(line) | fg(th.dim, ui_));
        }
    const int budget = std::max(3, height - static_cast<int>(rows.size()) - static_cast<int>(about.size()) - 1);
    const auto follow = pick_ != add_follow_ ? std::optional{pick_} : std::nullopt;
    add_follow_ = pick_;
    rows.push_back(scrolled(std::move(items), add_scroll_, budget, follow, ui_));
    rows.push_back(filler());
    for (auto& a : about) rows.push_back(std::move(a));
    return frame_panel(kInspector, vbox(std::move(rows)), {}, tr("tui.pipeline.add_title"));
}

bool PipelineScreen::on_add_key(const ftxui::Event& e) {
    using ftxui::Event;
    std::vector<Command> all;
    for (const auto& t : pipeline::block_types()) all.push_back({t.id, block_name(t.id), {}});
    const auto found = filter_commands(all, query_);
    if (e == Event::Escape) return adding_ = false, true;
    if (e.is_mouse()) {
        auto copy = e;
        const auto m = copy.mouse();
        const auto& box = panel_boxes_[kInspector];
        if (wheel_scroll(m, box, add_scroll_)) return true;
        if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed && box.Contain(m.x, m.y))
            for (const auto& [type, row] : add_rows_)
                if (row.Contain(m.x, m.y)) return add_block(type), true;
        return true;
    }
    if (e == Event::ArrowDown) return pick_ = found.empty() ? 0 : std::min(pick_ + 1, found.size() - 1), true;
    if (e == Event::ArrowUp) return pick_ = pick_ > 0 ? pick_ - 1 : 0, true;
    if (e == Event::Backspace) {
        while (!query_.empty() && (static_cast<unsigned char>(query_.back()) & 0xC0) == 0x80) query_.pop_back();
        if (!query_.empty()) query_.pop_back();
        pick_ = 0;
        return true;
    }
    if (e == Event::Return) {
        if (found.empty()) return adding_ = false, true;
        add_block(found[std::min(pick_, found.size() - 1)]->id);
        return true;
    }
    if (e.is_character()) {
        query_ += e.character();
        pick_ = 0;
        return true;
    }
    return true;
}

void PipelineScreen::add_block(const std::string& type) {
    adding_ = false;
    if (model_->selection.kind == Selection::Kind::wire) report(model_->insert_into_wire(model_->selection.wire, type));
    else if (const auto id = model_->add_node(type); !id.empty()) model_->selection = {Selection::Kind::node, id, {}};
    follow_ = true;
}

std::vector<std::string> PipelineScreen::param_values(const pipeline::BlockType& type, const pipeline::ParamSpec& spec) const {
    std::vector<std::string> out;
    if (type.id == "mra" && spec.name == "component") {
        out.emplace_back("a");
        for (int k = 1; k <= 16; ++k) out.push_back("d" + std::to_string(k));
        return out;
    }
    if (type.id == "check" && spec.name == "delay") return {"0", "auto"};
    if (spec.name == "wavelet") {
        if (spec.fallback == "auto") out.emplace_back("auto");
        for (auto& name : core::all_wavelet_names()) out.push_back(std::move(name));
    } else if (spec.name == "filter" && spec.fallback.find('.') != std::string::npos) {
        for (auto& name : core::named_filter_names()) out.push_back(std::move(name));
        for (const auto& name : core::all_wavelet_names())
            for (const char* role : {".h0", ".h1", ".f0", ".f1"}) out.push_back(std::string{name} + role);
    }
    return out;
}

void PipelineScreen::edit_param(int step) {
    const auto* n = selected_node();
    if (!n || n->type->params.empty()) return;
    const auto& spec = n->type->params[std::min(param_, n->type->params.size() - 1)];
    const auto current = n->params.count(spec.name) ? n->params.at(spec.name) : spec.fallback;
    const auto id = n->id;
    if (spec.kind == pipeline::ParamSpec::Kind::choice && !spec.choices.empty() && step == 0 && ui_.choose) {
        std::vector<Command> items;
        for (const auto& v : spec.choices)
            items.push_back({v, v, [this, id, name = spec.name, v] { report(model_->set_param(id, name, v)); }, value_note(n->type->id, spec.name, v).value_or("")});
        ui_.choose(id + "." + spec.name, std::move(items));
        return;
    }
    if (spec.kind == pipeline::ParamSpec::Kind::choice && !spec.choices.empty()) {
        auto it = std::find(spec.choices.begin(), spec.choices.end(), current);
        auto k = it == spec.choices.end() ? 0 : static_cast<long>(it - spec.choices.begin());
        k = (k + (step == 0 ? 1 : step) + static_cast<long>(spec.choices.size())) % static_cast<long>(spec.choices.size());
        report(model_->set_param(id, spec.name, spec.choices[static_cast<std::size_t>(k)]));
        return;
    }
    const auto name = spec.name;
    if (const auto names = param_values(*n->type, spec); !names.empty()) {
        if (step != 0) {
            auto it = std::find(names.begin(), names.end(), current);
            auto k = it == names.end() ? 0 : static_cast<long>(it - names.begin());
            k = (k + step + static_cast<long>(names.size())) % static_cast<long>(names.size());
            report(model_->set_param(id, name, names[static_cast<std::size_t>(k)]));
            return;
        }
        if (ui_.choose) {
            std::vector<Command> items;
            for (const auto& v : names) items.push_back({v, v, [this, id, name, v] { report(model_->set_param(id, name, v)); }, value_note(n->type->id, name, v).value_or("")});
            if (ui_.ask_text)
                items.push_back({"manual", tr("tui.pipeline.type_value"),
                                 [this, id, name, current] { ui_.ask_text(id + "." + name, current, [this, id, name](std::string value) { report(model_->set_param(id, name, value)); }); }});
            ui_.choose(id + "." + name, std::move(items));
            return;
        }
    }
    if (step != 0 || !ui_.ask_text) return;
    ui_.ask_text(id + "." + name, current, [this, id, name](std::string value) { report(model_->set_param(id, name, value)); });
}

bool PipelineScreen::on_inspector_key(const ftxui::Event& e) {
    using ftxui::Event;
    const auto* n = selected_node();
    if (!n) return false;
    if (e == Event::ArrowDown) return param_ = std::min(param_ + 1, n->type->params.empty() ? 0 : n->type->params.size() - 1), true;
    if (e == Event::ArrowUp) return param_ = param_ > 0 ? param_ - 1 : 0, true;
    if (e == Event::ArrowRight) return edit_param(1), true;
    if (e == Event::ArrowLeft) return edit_param(-1), true;
    if (e == Event::Return) return edit_param(0), true;
    return false;
}

ftxui::Element PipelineScreen::run_panel(int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto run = model_->run_view();
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const int inner = std::max(10, width);
    Elements head, nodes, foot;
    if (run.number == 0) {
        for (const auto& line : wrap_codepoints(tr("tui.pipeline.run_hint"), inner, "")) head.push_back(text(line) | fg(th.dim, ui_));
    } else {
        double done = 0;
        for (const auto& [id, n] : run.nodes) done += n.status.fraction;
        done /= static_cast<double>(std::max<std::size_t>(1, run.nodes.size()));
        auto title = fmt::format(fmt::runtime(tr("tui.pipeline.run_number")), fmt::arg("n", run.number));
        if (run.running) title += " · " + format_fixed(100 * done, 0) + " % · " + fmt::format(fmt::runtime(tr("tui.pipeline.threads")), fmt::arg("n", tr_count("unit.thread", static_cast<long long>(std::thread::hardware_concurrency()))));
        else if (run.cancelled) title += " · " + tr("tui.pipeline.cancelled");
        else if (run.error) title += " · " + tr("tui.pipeline.failed");
        else title += " · " + format_si(run.seconds, tr("unit.s"));
        head.push_back(text(ellipsize(title, inner, ascii)) | fg(run.error ? th.err : th.fg, ui_));
        if (run.error)
            for (const auto& line : wrap_codepoints(*run.error, inner, "")) head.push_back(text(line) | fg(th.err, ui_));
        if (const auto* g = model_->graph())
            for (const auto i : g->order) {
                const auto it = run.nodes.find(g->nodes[i].id);
                if (it == run.nodes.end()) continue;
                const auto& st = it->second.status;
                std::string mark = std::string{glyph(Glyph::bullet_off, ui_.caps.glyphs)};
                auto color = th.dim;
                switch (st.state) {
                case pipeline::NodeState::running: mark = std::string{glyph(Glyph::bullet_on, ui_.caps.glyphs)}, color = th.accent; break;
                case pipeline::NodeState::done: mark = std::string{glyph(Glyph::check, ui_.caps.glyphs)}, color = th.ok; break;
                case pipeline::NodeState::cached: mark = "=", color = th.ok; break;
                case pipeline::NodeState::failed: mark = std::string{glyph(Glyph::cross, ui_.caps.glyphs)}, color = th.err; break;
                case pipeline::NodeState::bypassed: mark = ascii ? "-" : "–"; break;
                default: break;
                }
                const auto time = st.state == pipeline::NodeState::running ? eighths_bar(st.fraction, 8)
                                  : st.state == pipeline::NodeState::waiting ? std::string{"—"}
                                                                            : format_si(st.wall_seconds, tr("unit.s"));
                nodes.push_back(hbox({text(mark + " ") | fg(color, ui_), text(pad_right(ellipsize(st.id, 10, ascii), 10)) | fg(th.fg, ui_), text(pad_left(time, 10)) | fg(th.dim, ui_),
                                      text(pad_left(st.bytes ? format_si(static_cast<double>(st.bytes), tr("unit.byte")) : std::string{"—"}, 10)) | fg(th.dim, ui_),
                                      text(it->second.bottleneck ? " !" : "") | fg(th.warn, ui_)}));
            }
    }
    std::vector<std::string> parts{"$ signals run " + (model_->path() ? model_->path()->filename().string() : tr("tui.pipeline.untitled"))};
    for (const auto& set : run.sets) parts.push_back("--set " + set);
    if (run.structure_changed || !model_->path()) {
        for (const auto& line : wrap_codepoints(tr("tui.pipeline.save_for_cli"), inner, "")) foot.push_back(text(line) | fg(th.dim, ui_));
        parts.clear();
    }
    std::string line;
    for (const auto& part : parts) {
        if (!line.empty() && display_width(line) + 1 + display_width(part) > inner - 2) {
            foot.push_back(text(line + " \\") | fg(th.dim, ui_));
            line = "  ";
        }
        line += (line.empty() || line == "  " ? "" : " ") + part;
    }
    if (!line.empty())
        for (const auto& piece : wrap_codepoints(line, inner, "  ")) foot.push_back(text(piece) | fg(th.dim, ui_));
    foot.push_back(hbox({key_label(" r ", ui_), text(" " + tr("tui.pipeline.hint.run") + "  ") | fg(th.dim, ui_), key_label(" Esc ", ui_),
                         text(" " + tr("tui.pipeline.hint.cancel")) | fg(th.dim, ui_)}));
    const int budget = std::max(1, height - static_cast<int>(head.size()) - static_cast<int>(foot.size()));
    Elements rows = std::move(head);
    rows.push_back(scrolled(std::move(nodes), run_scroll_, budget, std::nullopt, ui_));
    rows.push_back(filler());
    for (auto& f : foot) rows.push_back(std::move(f));
    return frame_panel(kRun, vbox(std::move(rows)), {}, tr("tui.pipeline.run"));
}

void PipelineScreen::refresh_probe(const RunView& run) {
    std::vector<std::string> names;
    for (const auto& [name, value] : run.probes) names.push_back(name);
    const std::size_t chips = names.size() + (has_sweep() ? 1 : 0);
    if (chips == 0) {
        probe_ = {};
        return;
    }
    probe_index_ = std::min(probe_index_, chips - 1);
    if (probe_index_ >= names.size()) {
        probe_ = {};
        return;
    }
    const auto& name = names[probe_index_];
    const bool spectrum = probe_view_ == ProbeView::spectrum;
    if (probe_.run == run.number && probe_.name == name && (!spectrum || !probe_.db.empty() || !probe_.samples)) return;
    if (probe_.run != run.number || probe_.name != name) {
        probe_ = {};
        probe_.run = run.number;
        probe_.name = name;
        const auto* audio = std::get_if<core::AudioBuffer>(run.probes.at(name).get());
        if (!audio || audio->channels.empty()) return;
        auto samples = std::make_shared<std::vector<std::vector<float>>>();
        for (const auto& c : audio->channels) samples->emplace_back(c.begin(), c.end());
        probe_.samples = samples;
        for (const auto& c : *samples) probe_.pyramids.push_back(std::make_unique<MinMaxPyramid>(std::span<const float>{c}));
        probe_.rate = audio->sample_rate;
        probe_.frames = audio->frames();
    }
    if (spectrum && probe_.db.empty() && probe_.samples) {
        const auto* audio = std::get_if<core::AudioBuffer>(run.probes.at(name).get());
        std::size_t segment = 4096;
        while (segment > 16 && segment > audio->frames()) segment /= 2;
        for (const auto& c : audio->channels)
            if (auto psd = core::welch_psd(c, audio->sample_rate, segment, segment / 2)) {
                probe_.freqs = core::welch_frequencies(audio->sample_rate, segment);
                auto& db = probe_.db.emplace_back();
                for (const double v : *psd) db.push_back(10 * std::log10(std::max(v, 1e-30)));
            }
    }
}

namespace {

const core::AudioBuffer* probe_signal(const RunView& run, const std::string& name) {
    const auto it = run.probes.find(name);
    return it == run.probes.end() || !it->second ? nullptr : std::get_if<core::AudioBuffer>(it->second.get());
}

std::string probe_table(const core::AudioBuffer& audio, char separator, std::size_t limit) {
    std::string out = "n";
    for (std::size_t c = 0; c < audio.channels.size(); ++c) out += separator + ("ch" + std::to_string(c));
    out += '\n';
    const auto rows = std::min(audio.frames(), limit);
    for (std::size_t n = 0; n < rows; ++n) {
        out += std::to_string(n);
        for (const auto& ch : audio.channels) out += separator + fmt::format("{:.15g}", ch[n]);
        out += '\n';
    }
    return out;
}

}  // namespace

ftxui::Element PipelineScreen::probe_values(const core::AudioBuffer& audio, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const std::size_t frames = audio.frames(), channels = audio.channels.size();
    const auto window = list_window(probe_text_scroll_.first, frames, std::max(1, height - 1), 1, std::nullopt);
    const int gap = 2;
    const std::string space(gap, ' ');
    const int index_width = display_width(std::to_string(frames > 0 ? frames - 1 : 0));
    const auto label = [](std::size_t c) { return "ch" + std::to_string(c); };
    std::vector<std::vector<std::string>> cells(channels);
    std::vector<int> widths(channels);
    int all = index_width;
    for (std::size_t c = 0; c < channels; ++c) {
        widths[c] = display_width(label(c));
        for (std::size_t k = window.first; k < window.first + window.shown; ++k) {
            cells[c].push_back(scientific_ ? format_scientific(audio.channels[c][k], value_digits_) : format_number(audio.channels[c][k], value_digits_));
            widths[c] = std::max(widths[c], display_width(cells[c].back()));
        }
        all += gap + widths[c];
    }
    const auto more = [](std::size_t hidden) { return fmt::format(fmt::runtime(tr("tui.pipeline.values.more_channels")), fmt::arg("count", hidden)); };
    std::size_t shown = channels;
    if (all > width) {
        shown = 0;
        for (int used = index_width; shown < channels; ++shown) {
            const int next = used + gap + widths[shown];
            const std::size_t hidden = channels - shown - 1;
            if (shown > 0 && next + (hidden > 0 ? gap + display_width(more(hidden)) : 0) > width) break;
            used = next;
        }
    }
    Elements header{text(pad_left("n", index_width)) | fg(th.dim, ui_)};
    for (std::size_t c = 0; c < shown; ++c) header.push_back(text(space + pad_left(label(c), widths[c])) | fg(th.dim, ui_));
    if (shown < channels) header.push_back(text(space + more(channels - shown)) | fg(th.warn, ui_));
    Elements lines;
    for (std::size_t r = 0; r < window.shown; ++r) {
        Elements row{text(pad_left(std::to_string(window.first + r), index_width)) | fg(th.dim, ui_)};
        for (std::size_t c = 0; c < shown; ++c) row.push_back(text(space + pad_left(cells[c][r], widths[c])) | fg(th.fg, ui_));
        lines.push_back(hbox(std::move(row)));
    }
    return vbox({hbox(std::move(header)), scrolled(window, frames, std::move(lines), ui_)});
}


void PipelineScreen::refresh_compare(const RunView& run) {
    if (!compare_) return;
    const auto* b = probe_signal(run, *compare_);
    const auto* a = probe_signal(run, probe_.name);
    if (!a || !b || a->channels.empty() || b->channels.empty() || *compare_ == probe_.name) {
        compare_.reset();
        other_ = {};
        compare_stats_.clear();
        return;
    }
    const auto key = std::to_string(run.number) + "\n" + probe_.name + "\n" + *compare_;
    if (key == compare_key_) return;
    compare_key_ = key;
    other_ = {};
    other_.run = run.number;
    other_.name = *compare_;
    auto samples = std::make_shared<std::vector<std::vector<float>>>();
    for (const auto& ch : b->channels) samples->emplace_back(ch.begin(), ch.end());
    other_.samples = samples;
    for (const auto& ch : *samples) other_.pyramids.push_back(std::make_unique<MinMaxPyramid>(std::span<const float>{ch}));
    other_.rate = b->sample_rate;
    other_.frames = b->frames();
    const auto frames = std::min(a->frames(), b->frames());
    const auto channels = std::min(a->channels.size(), b->channels.size());
    double worst = 0, difference = 0, energy = 0;
    for (std::size_t ch = 0; ch < channels; ++ch)
        for (std::size_t n = 0; n < frames; ++n) {
            const double d = a->channels[ch][n] - b->channels[ch][n];
            worst = std::max(worst, std::abs(d));
            difference += d * d;
            energy += a->channels[ch][n] * a->channels[ch][n];
        }
    const auto count = static_cast<double>(std::max<std::size_t>(1, frames * channels));
    const double snr = difference > 0 ? 10 * std::log10(energy / difference) : std::numeric_limits<double>::infinity();
    compare_stats_ = fmt::format(fmt::runtime(tr("tui.pipeline.compare.stats")), fmt::arg("name", *compare_), fmt::arg("max", format_number(worst, 4)),
                                 fmt::arg("rms", format_number(std::sqrt(difference / count), 4)), fmt::arg("snr", format_number(snr, 4)));
    if (frames != a->frames() || frames != b->frames() || channels != a->channels.size() || channels != b->channels.size())
        compare_stats_ += " · " + fmt::format(fmt::runtime(tr("tui.pipeline.compare.shorter")), fmt::arg("frames", frames), fmt::arg("channels", channels));
    compare_correlation_.clear();
    const auto& x = a->channels.front();
    const auto& y = b->channels.front();
    const double norms = std::sqrt(core::energy(x) * core::energy(y));
    if (const auto z = core::correlate(y, x, core::CorrelationMode::full); z && norms > 0) {
        double zero = 0;
        for (std::size_t n = 0; n < std::min(x.size(), y.size()); ++n) zero += x[n] * y[n];
        std::size_t k = 0;
        for (std::size_t i = 1; i < z->size(); ++i)
            if (std::abs((*z)[i]) > std::abs((*z)[k])) k = i;
        const auto lag = static_cast<long long>(k) - static_cast<long long>(x.size() - 1);
        compare_correlation_ = fmt::format(fmt::runtime(tr("tui.pipeline.compare.correlation")), fmt::arg("zero", format_number(zero / norms, 4)),
                                           fmt::arg("peak", format_number((*z)[k] / norms, 4)), fmt::arg("lag", fmt::format("{:+}", lag)));
    }
}

void PipelineScreen::toggle_compare() {
    if (compare_) {
        compare_.reset();
        other_ = {};
        compare_stats_.clear();
        compare_correlation_.clear();
        compare_key_.clear();
        notice_ = tr("tui.pipeline.compare.off");
        return;
    }
    const auto run = model_->run_view();
    refresh_probe(run);
    std::vector<std::string> others;
    for (const auto& [name, value] : run.probes)
        if (name != probe_.name && probe_signal(run, name)) others.push_back(name);
    if (others.empty() || !probe_signal(run, probe_.name)) {
        notice_ = tr("tui.pipeline.compare.none");
        return;
    }
    if (others.size() == 1 || !ui_.choose) {
        compare_ = others.front();
        return;
    }
    std::vector<Command> items;
    for (const auto& name : others) items.push_back({name, name, [this, name] { compare_ = name; }});
    ui_.choose(fmt::format(fmt::runtime(tr("tui.pipeline.compare.choose")), fmt::arg("name", probe_.name)), std::move(items));
}

void PipelineScreen::copy_probe() {
    const auto run = model_->run_view();
    const auto* audio = probe_signal(run, probe_.name);
    if (!audio) {
        notice_ = tr("tui.pipeline.probe.not_signal");
        return;
    }
    constexpr std::size_t kCopyRows = 100000;
    if (ui_.copy_text) ui_.copy_text(probe_table(*audio, '\t', kCopyRows));
    notice_ = audio->frames() > kCopyRows ? fmt::format(fmt::runtime(tr("tui.pipeline.probe.copied_part")), fmt::arg("rows", kCopyRows), fmt::arg("total", audio->frames()))
                                          : fmt::format(fmt::runtime(tr("tui.pipeline.probe.copied")), fmt::arg("rows", audio->frames()));
}

void PipelineScreen::save_probe() {
    if (!probe_signal(model_->run_view(), probe_.name)) {
        notice_ = tr("tui.pipeline.probe.not_signal");
        return;
    }
    if (!ui_.ask_text) return;
    ui_.ask_text(tr("tui.pipeline.probe.save_prompt"), probe_.name + ".csv", [this, name = probe_.name](std::string answer) {
        const auto run = model_->run_view();
        const auto* audio = probe_signal(run, name);
        if (!audio) return;
        auto path = utf8_file(answer);
        if (path.is_relative()) path = model_->base_dir() / path;
        const auto ext = path.extension().string();
        core::Result<void> written;
        if (ext == ".wav") {
            if (auto r = io::write_wav(path, *audio, {io::SampleFormat::float32, false}); !r) written = std::unexpected{r.error()};
        } else if (ext == ".csv") {
            written = io::write_file_atomic(path, probe_table(*audio, ',', audio->frames()));
        } else {
            notice_ = tr("tui.pipeline.probe.save_kinds");
            return;
        }
        notice_ = written ? fmt::format(fmt::runtime(tr("tui.pipeline.probe.saved")), fmt::arg("path", path.string())) : written.error().message;
    });
}

void PipelineScreen::duplicate_selected() {
    const auto* n = selected_node();
    if (!n) {
        notice_ = tr("tui.pipeline.duplicate.pick");
        return;
    }
    const auto id = model_->duplicate_node(n->id);
    notice_ = id.empty() ? std::string{} : fmt::format(fmt::runtime(tr("tui.pipeline.duplicate.done")), fmt::arg("id", id));
}

bool PipelineScreen::has_sweep() const {
    const auto s = model_->sweep_view();
    return s.running || s.result || s.error;
}

void PipelineScreen::start_sweep() {
    const auto* n = selected_node();
    if (!n) {
        notice_ = tr("tui.pipeline.sweep.pick_node");
        return;
    }
    if (n->type->params.empty()) {
        notice_ = fmt::format(fmt::runtime(tr("tui.pipeline.sweep.no_params")), fmt::arg("node", n->id));
        return;
    }
    if (!ui_.choose) return;
    std::vector<Command> items;
    for (const auto& spec : n->type->params) {
        const auto current = n->params.count(spec.name) ? n->params.at(spec.name) : spec.fallback;
        items.push_back({spec.name, spec.name + " = " + current, [this, id = n->id, spec, current] { ask_sweep_values(id, spec, current); }});
    }
    ui_.choose(fmt::format(fmt::runtime(tr("tui.pipeline.sweep.choose_param")), fmt::arg("node", n->id)), std::move(items));
}

namespace {

std::string sweep_suggestion(const pipeline::ParamSpec& spec, const std::string& current) {
    using Kind = pipeline::ParamSpec::Kind;
    if (spec.kind == Kind::choice && !spec.choices.empty()) {
        std::string out;
        for (const auto& c : spec.choices) out += (out.empty() ? "" : ",") + c;
        return out;
    }
    const auto v = pipeline::numeric(current);
    if (!v || (spec.kind != Kind::integer && spec.kind != Kind::real)) return current;
    double lo = *v == 0 ? 0.0 : std::min(*v / 2, *v * 2), hi = *v == 0 ? (spec.kind == Kind::integer ? 10.0 : 1.0) : std::max(*v / 2, *v * 2);
    lo = std::max(lo, spec.min);
    hi = std::min(hi, spec.max);
    if (spec.kind == Kind::integer) {
        lo = std::ceil(lo);
        hi = std::floor(hi);
        if (hi <= lo) return current;
        return fmt::format("{:.0f}:{:.0f}:{:.0f}", lo, hi, std::max(1.0, std::round((hi - lo) / 10)));
    }
    if (hi <= lo) return current;
    return fmt::format("{}:{}@11", format_number(lo, 6), format_number(hi, 6));
}

bool geometric(const std::vector<double>& xs) {
    if (xs.size() < 3 || xs.front() <= 0) return false;
    const double ratio = xs[1] / xs[0];
    if (ratio <= 1.0 + 1e-9) return false;
    for (std::size_t k = 1; k < xs.size(); ++k)
        if (xs[k] <= 0 || std::abs(xs[k] / xs[k - 1] - ratio) > 1e-6 * ratio) return false;
    return true;
}

}  // namespace

void PipelineScreen::ask_sweep_values(const std::string& node, const pipeline::ParamSpec& spec, const std::string& current) {
    if (!ui_.ask_text) return;
    ui_.ask_text(fmt::format(fmt::runtime(tr("tui.pipeline.sweep.values_prompt")), fmt::arg("target", node + "." + spec.name)), sweep_suggestion(spec, current),
                 [this, target = node + "." + spec.name](std::string text) {
                     auto axis = pipeline::parse_vary(target + "=" + text);
                     if (!axis) {
                         notice_ = axis.error().message;
                         return;
                     }
                     choose_sweep_measure(std::move(*axis));
                 });
}

void PipelineScreen::choose_sweep_measure(pipeline::SweepAxis axis) {
    if (!ui_.choose) return;
    const auto start = [this, axis](std::vector<pipeline::Measure> measures) {
        if (!ui_.jobs) return;
        model_->sweep(*ui_.jobs, axis, std::move(measures));
        probe_index_ = model_->run_view().probes.size();
        probe_text_scroll_ = {};
        focus_ = kProbe;
        layout_.show_panel(kProbe);
        notice_.clear();
    };
    std::vector<Command> items;
    const auto run = model_->run_view();
    for (const auto& nr : run.reports)
        for (const auto& [name, field] : nr.report.fields) {
            const auto value = std::holds_alternative<double>(field) ? std::optional{std::get<double>(field)} : pipeline::numeric(std::get<std::string>(field));
            if (!value) continue;
            const auto id = nr.node + "." + name;
            items.push_back({id, id, [start, id] { start({*pipeline::parse_measure(id)}); },
                             fmt::format(fmt::runtime(tr("tui.pipeline.sweep.report_field")), fmt::arg("value", format_number(*value, 4)))});
        }
    std::vector<std::string> nodes;
    if (const auto* g = model_->graph())
        for (const auto& n : g->nodes)
            if (std::any_of(n.type->outputs.begin(), n.type->outputs.end(),
                            [](const pipeline::PortSpec& p) { return p.type == pipeline::PortType::signal || p.type == pipeline::PortType::coeffs; }))
                nodes.push_back(n.id);
    std::stable_partition(nodes.begin(), nodes.end(), [&](const std::string& id) { return id == axis.node; });
    for (const auto& id : nodes)
        for (const char* metric : {"rms", "peak", "energy"}) {
            const auto text = id + ":" + metric;
            items.push_back({text, text, [start, text] { start({*pipeline::parse_measure(text)}); }, tr(std::string{"tui.pipeline.sweep.metric."} + metric)});
        }
    if (ui_.ask_text)
        items.push_back({"manual", tr("tui.pipeline.sweep.type_measure"), [this, start] {
                             ui_.ask_text(tr("tui.pipeline.sweep.measure_prompt"), "", [this, start](std::string text) {
                                 std::vector<pipeline::Measure> measures;
                                 std::replace(text.begin(), text.end(), ',', ' ');
                                 std::istringstream in{text};
                                 for (std::string word; in >> word;) {
                                     auto m = pipeline::parse_measure(word);
                                     if (!m) {
                                         notice_ = m.error().message;
                                         return;
                                     }
                                     measures.push_back(std::move(*m));
                                 }
                                 if (!measures.empty()) start(std::move(measures));
                             });
                         }});
    ui_.choose(fmt::format(fmt::runtime(tr("tui.pipeline.sweep.choose_measure")), fmt::arg("target", axis.node + "." + axis.param),
                           fmt::arg("count", axis.values.size())),
               std::move(items));
}

ftxui::Element PipelineScreen::sweep_body(const SweepView& sweep, int width, int rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    if (sweep.running)
        return paragraph(fmt::format(fmt::runtime(tr("tui.pipeline.sweep.progress")), fmt::arg("done", sweep.done), fmt::arg("total", sweep.total))) | fg(th.dim, ui_);
    if (sweep.error) return paragraph(*sweep.error) | fg(th.err, ui_);
    if (!sweep.result) return text("");
    const auto& r = *sweep.result;
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::string missing = ascii ? "-" : "—";
    if (probe_view_ == ProbeView::values) {
        const bool errors = std::any_of(r.rows.begin(), r.rows.end(), [](const pipeline::SweepRow& row) { return !row.error.empty(); });
        std::vector<std::string> header{r.axis.node + "." + r.axis.param};
        for (const auto& m : r.measures) header.push_back(m.text);
        std::vector<int> widths;
        for (const auto& h : header) widths.push_back(display_width(h));
        const auto window = list_window(probe_text_scroll_.first, r.rows.size(), std::max(1, rows - 1), 1, std::nullopt);
        std::vector<std::vector<std::string>> cells;
        for (std::size_t k = window.first; k < window.first + window.shown; ++k) {
            const auto& row = r.rows[k];
            auto& line = cells.emplace_back(std::vector<std::string>{row.value});
            for (const auto& v : row.results) line.push_back(v ? format_number(*v, 6) : missing);
            for (std::size_t c = 0; c < line.size(); ++c) widths[c] = std::max(widths[c], display_width(line[c]));
        }
        const std::string space(2, ' ');
        int used = 0;
        std::size_t shown = 0;
        for (; shown < widths.size(); ++shown) {
            const int next = used + (shown ? 2 : 0) + widths[shown];
            if (shown > 0 && next > width) break;
            used = next;
        }
        const auto cell = [&](std::size_t c, const std::string& s) { return (c ? space : std::string{}) + (c ? pad_left(s, widths[c]) : pad_right(s, widths[c])); };
        Elements head;
        for (std::size_t c = 0; c < shown; ++c) head.push_back(text(cell(c, header[c])) | fg(th.dim, ui_));
        if (errors && used + 2 < width) head.push_back(text(space + tr("tui.pipeline.sweep.error_column")) | fg(th.dim, ui_));
        Elements lines;
        for (std::size_t k = 0; k < cells.size(); ++k) {
            Elements line;
            for (std::size_t c = 0; c < shown; ++c) line.push_back(text(cell(c, cells[k][c])) | fg(c ? th.fg : th.accent, ui_));
            if (const auto& e = r.rows[window.first + k].error; !e.empty() && used + 2 < width)
                line.push_back(text(space + ellipsize(e, std::max(4, width - used - 2), ascii)) | fg(th.err, ui_));
            lines.push_back(hbox(std::move(line)));
        }
        return vbox({hbox(std::move(head)), scrolled(window, r.rows.size(), std::move(lines), ui_)});
    }
    std::vector<double> xs;
    bool numeric_x = true;
    for (const auto& row : r.rows)
        if (const auto v = pipeline::numeric(row.value)) xs.push_back(*v);
        else numeric_x = false;
    if (!numeric_x) {
        xs.clear();
        for (std::size_t k = 0; k < r.rows.size(); ++k) xs.push_back(static_cast<double>(k));
    }
    PlotSpec spec;
    double ylo = std::numeric_limits<double>::infinity(), yhi = -ylo;
    for (std::size_t j = 0; j < r.measures.size(); ++j) {
        Trace t{{}, {}, th.trace[j % th.trace.size()]};
        for (std::size_t k = 0; k < r.rows.size(); ++k)
            if (const auto& v = r.rows[k].results[j]; v && std::isfinite(*v)) {
                t.x.push_back(xs[k]);
                t.y.push_back(*v);
                ylo = std::min(ylo, *v);
                yhi = std::max(yhi, *v);
            }
        t.stems = t.y.size() == 1;
        spec.traces.push_back(std::move(t));
    }
    if (!(ylo <= yhi)) return paragraph(tr("tui.pipeline.sweep.no_values")) | fg(th.dim, ui_);
    const double pad = yhi > ylo ? (yhi - ylo) * 0.05 : std::max(1e-12, std::abs(yhi) * 0.1 + (yhi == 0 ? 1 : 0));
    spec.y = {ylo - pad, yhi + pad};
    const auto [xmin, xmax] = std::minmax_element(xs.begin(), xs.end());
    const bool log = numeric_x && geometric(xs);
    spec.x = probe_zoom_.axis("probe_sweep", {*xmin, *xmax > *xmin ? *xmax : *xmin + 1, log ? AxisScale::log10 : AxisScale::linear}).axis();
    if (xs.size() <= 12) spec.x_marks = xs;
    Elements out;
    std::vector<std::string> names;
    for (const auto& m : r.measures) names.push_back(m.text);
    if (names.size() > 1) {
        Elements legend;
        for (std::size_t j = 0; j < names.size(); ++j)
            legend.push_back(text((j ? "  " : "") + std::string{ascii ? "# " : "■ "} + names[j]) | fg(th.trace[j % th.trace.size()], ui_));
        out.push_back(hbox(std::move(legend)));
    }
    if (!numeric_x) {
        std::string labels;
        for (std::size_t k = 0; k < r.rows.size(); ++k) labels += (k ? "  " : "") + std::to_string(k) + "=" + r.rows[k].value;
        out.push_back(text(ellipsize(labels, std::max(10, width), ascii)) | fg(th.dim, ui_));
    }
    const int room = std::max(2, rows - static_cast<int>(out.size()));
    auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, room);
    const auto param = r.axis.node + "." + r.axis.param;
    out.push_back(probe_zoom_.track("probe_sweep", std::move(spec), std::move(drawn),
                                    {tr("tui.pipeline.sweep.chip") + " · " + param, names, numeric_x ? param : param + " (#)", ""}));
    return vbox(std::move(out));
}

void PipelineScreen::name_probe() {
    const auto* g = model_->graph();
    if (!g || !ui_.ask_text) return;
    const auto on = [&](const pipeline::PortRef& at) -> const pipeline::Probe* {
        for (const auto& p : g->probes)
            if (p.at.node == at.node && p.at.port == at.port) return &p;
        for (const auto& p : g->probes)
            if (p.at.node == at.node) return &p;
        return nullptr;
    };
    const auto& sel = model_->selection;
    const pipeline::Probe* target = nullptr;
    if (focus_ != kProbe && sel.kind == Selection::Kind::node) target = on({sel.node, {}, 0, 0});
    if (focus_ != kProbe && sel.kind == Selection::Kind::wire) target = on(sel.wire.from);
    for (const auto& p : g->probes)
        if (!target && p.name == probe_.name) target = &p;
    if (!target) {
        notice_ = tr("tui.pipeline.no_probe");
        return;
    }
    ui_.ask_text(tr("tui.pipeline.probe_name"), target->name, [this, at = target->at](std::string name) {
        const auto r = model_->rename_probe(at, std::move(name));
        report(r);
        const auto* graph = model_->graph();
        if (!r || !graph) return;
        const auto renamed = std::find_if(graph->probes.begin(), graph->probes.end(), [&](const auto& p) { return p.at.node == at.node && p.at.port == at.port; });
        if (renamed == graph->probes.end()) return;
        const auto probes = model_->run_view().probes;
        if (const auto it = probes.find(renamed->name); it != probes.end()) probe_index_ = static_cast<std::size_t>(std::distance(probes.begin(), it));
    });
}

ftxui::Element PipelineScreen::channel_legend(std::size_t channels, int width, bool keys) const {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const std::string mark = ui_.caps.glyphs == GlyphMode::ascii ? "#" : "■";
    const auto hint = keys ? "  " + tr("tui.pipeline.legend_keys") : std::string{};
    Elements parts;
    int used = display_width(hint);
    std::size_t shown = 0;
    for (; shown < channels; ++shown) {
        const auto item = mark + " ch" + std::to_string(shown) + "  ";
        const auto rest = channels - shown - 1;
        const int tail = rest > 0 ? display_width(fmt::format(fmt::runtime(tr("tui.pipeline.values.more_channels")), fmt::arg("count", rest))) : 0;
        if (shown > 0 && used + display_width(item) + tail > width) break;
        parts.push_back(text(item) | fg(th.trace[shown % th.trace.size()], ui_));
        used += display_width(item);
    }
    if (shown < channels) parts.push_back(text(fmt::format(fmt::runtime(tr("tui.pipeline.values.more_channels")), fmt::arg("count", channels - shown))) | fg(th.warn, ui_));
    parts.push_back(filler());
    if (keys) parts.push_back(text(hint) | fg(th.dim, ui_));
    return hbox(std::move(parts));
}

ftxui::Element PipelineScreen::probe_time(int width, int rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const std::size_t channels = probe_.pyramids.size();
    const double rate = std::max(1.0, probe_.rate);
    const Axis full{0, static_cast<double>(probe_.frames) / rate};
    auto& master = probe_zoom_.axis("probe_time", full);
    const auto peak = [](const MinMaxPyramid& p) {
        const auto all = p.query(0, p.size(), 1);
        return all.empty() ? 1.0 : std::max({1e-12, std::abs(static_cast<double>(all[0].lo)), std::abs(static_cast<double>(all[0].hi))});
    };
    const auto run = model_->run_view();
    refresh_compare(run);
    const auto* exact = probe_.frames <= kStemFrames ? probe_signal(run, probe_.name) : nullptr;
    const auto* exact_other = compare_ && other_.frames <= kStemFrames ? probe_signal(run, *compare_) : nullptr;
    const bool overlay = compare_ && !other_.pyramids.empty();
    const auto make = [&](std::size_t from, std::size_t to, double m) {
        PlotSpec spec;
        spec.x = master.axis();
        spec.y = {-m, m};
        spec.playhead = playhead();
        if (exact) {
            std::vector<double> times(exact->frames());
            for (std::size_t n = 0; n < times.size(); ++n) times[n] = static_cast<double>(n) / rate;
            for (std::size_t c = from; c < to && c < exact->channels.size(); ++c) {
                Trace t{exact->channels[c], times, th.trace[c % th.trace.size()]};
                t.stems = true;
                spec.traces.push_back(std::move(t));
            }
        }
        if (overlay && exact_other) {
            std::vector<double> times(exact_other->frames());
            for (std::size_t n = 0; n < times.size(); ++n) times[n] = static_cast<double>(n) / std::max(1.0, other_.rate);
            for (std::size_t c = from; c < to && c < exact_other->channels.size(); ++c) spec.traces.push_back(Trace{exact_other->channels[c], times, th.warn});
        } else if (overlay) {
            const double other_rate = std::max(1.0, other_.rate);
            for (std::size_t c = from; c < to && c < other_.pyramids.size(); ++c) {
                const auto* pyramid = other_.pyramids[c].get();
                const auto first = static_cast<std::size_t>(std::clamp(spec.x.lo * other_rate, 0.0, static_cast<double>(pyramid->size())));
                const auto last = std::max(first + 1, static_cast<std::size_t>(std::clamp(spec.x.hi * other_rate, 0.0, static_cast<double>(pyramid->size()))));
                if (first < pyramid->size())
                    spec.envelopes.push_back(Envelope{{}, th.warn, [pyramid, first, last](int cols) { return pyramid->query(first, last, static_cast<std::size_t>(cols)); }, last - first});
            }
        }
        if (exact) return spec;
        for (std::size_t c = from; c < to; ++c) {
            const auto* pyramid = probe_.pyramids[c].get();
            const auto first = static_cast<std::size_t>(std::clamp(spec.x.lo * rate, 0.0, static_cast<double>(pyramid->size())));
            const auto last = std::max(first + 1, static_cast<std::size_t>(std::clamp(spec.x.hi * rate, 0.0, static_cast<double>(pyramid->size()))));
            spec.envelopes.push_back(
                Envelope{{}, th.trace[c % th.trace.size()], [pyramid, first, last](int cols) { return pyramid->query(first, last, static_cast<std::size_t>(cols)); }, last - first});
        }
        return spec;
    };
    const auto title = tr("tui.pipeline.probe") + " · " + probe_.name + " · " + tr("tui.pipeline.view.time");
    std::vector<std::string> names;
    for (std::size_t c = 0; c < channels; ++c) names.push_back("ch" + std::to_string(c));
    Elements out;
    if (channels > 1) out.push_back(channel_legend(channels, width, true));
    const bool correlation = overlay && !compare_correlation_.empty();
    if (overlay) out.push_back(text(ellipsize(compare_stats_, std::max(10, width), ui_.caps.glyphs == GlyphMode::ascii)) | fg(th.warn, ui_));
    if (correlation) out.push_back(text(ellipsize(compare_correlation_, std::max(10, width), ui_.caps.glyphs == GlyphMode::ascii)) | fg(th.warn, ui_));
    const int room = std::max(2, rows - (channels > 1 ? 1 : 0) - (overlay ? 1 : 0) - (correlation ? 1 : 0));
    if (!stacked_ || channels == 1) {
        double m = 1e-12;
        for (const auto& p : probe_.pyramids) m = std::max(m, peak(*p));
        if (overlay)
            for (const auto& p : other_.pyramids) m = std::max(m, peak(*p));
        auto spec = make(0, channels, m);
        auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, room);
        out.push_back(probe_zoom_.track("probe_time", std::move(spec), std::move(drawn), {title, names, tr("tui.export.time_s"), ""}));
        return vbox(std::move(out));
    }
    const auto fits = static_cast<std::size_t>(std::max(1, room / 2));
    const std::size_t shown = channels <= fits ? channels : std::max<std::size_t>(1, static_cast<std::size_t>(std::max(1, (room - 1) / 2)));
    const int each = std::max(2, (room - (shown < channels ? 1 : 0)) / static_cast<int>(shown));
    for (std::size_t c = 0; c < shown; ++c) {
        const auto id = c == 0 ? std::string{"probe_time"} : "probe_time:" + std::to_string(c);
        if (c > 0) probe_zoom_.axis(id, full) = master;
        auto spec = make(c, c + 1, overlay && c < other_.pyramids.size() ? std::max(peak(*probe_.pyramids[c]), peak(*other_.pyramids[c])) : peak(*probe_.pyramids[c]));
        auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, each);
        out.push_back(probe_zoom_.track(id, std::move(spec), std::move(drawn), {title + " · " + names[c], {names[c]}, tr("tui.export.time_s"), ""}));
    }
    if (shown < channels)
        out.push_back(text(fmt::format(fmt::runtime(tr("tui.pipeline.channels_hidden")), fmt::arg("count", channels - shown))) | fg(th.warn, ui_));
    return vbox(std::move(out));
}

void PipelineScreen::sync_stacked() {
    if (!stacked_) return;
    const Axis full{0, static_cast<double>(probe_.frames) / std::max(1.0, probe_.rate)};
    const auto master = probe_zoom_.view("probe_time");
    for (std::size_t c = 1; c < probe_.pyramids.size(); ++c) {
        const auto id = "probe_time:" + std::to_string(c);
        const auto other = probe_zoom_.view(id);
        if (master && other && (other->lo != master->lo || other->hi != master->hi)) {
            probe_zoom_.axis("probe_time", full) = probe_zoom_.axis(id, full);
            return;
        }
    }
}

std::optional<double> PipelineScreen::playhead() const {
    if (!ui_.player || ui_.player->state() != audio::PlayState::playing || !ui_.player->tag().starts_with("pipeline")) return std::nullopt;
    const auto run = model_->run_view();
    const auto* input = run.input ? std::get_if<core::AudioBuffer>(run.input.get()) : nullptr;
    const double rate = last_input_ && input ? input->sample_rate : probe_.rate;
    return rate > 0 ? std::optional{static_cast<double>(ui_.player->position()) / rate} : std::nullopt;
}

void PipelineScreen::play(bool input) {
    if (!ui_.player) return;
    const auto run = model_->run_view();
    refresh_probe(run);
    const core::AudioBuffer* source = nullptr;
    if (input && run.input) source = std::get_if<core::AudioBuffer>(run.input.get());
    if (!input && !probe_.name.empty() && run.probes.contains(probe_.name)) source = std::get_if<core::AudioBuffer>(run.probes.at(probe_.name).get());
    if (!source || source->channels.empty()) {
        notice_ = tr("tui.pipeline.nothing_to_play");
        return;
    }
    auto samples = std::make_shared<std::vector<std::vector<float>>>();
    for (const auto& c : source->channels) samples->emplace_back(c.begin(), c.end());
    audio::Clip clip;
    for (const auto& c : *samples) clip.channels.emplace_back(c);
    clip.rate = static_cast<std::uint32_t>(std::max(1.0, std::round(source->sample_rate)));
    clip.owner = samples;
    clip.tag = input ? "pipeline.input" : "pipeline.probe";
    last_input_ = input;
    if (ui_.player->state() == audio::PlayState::playing && ui_.player->tag().starts_with("pipeline")) {
        ui_.player->replace(std::move(clip));
        return;
    }
    if (auto r = ui_.player->play(std::move(clip), 0); !r) notice_ = fmt::format(fmt::runtime(tr("tui.signal.audio_unavailable")), fmt::arg("reason", r.error().message));
}

ftxui::Element PipelineScreen::probe_panel(int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto run = model_->run_view();
    refresh_probe(run);
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::array<std::string, 3> words{tr("tui.pipeline.view.time"), tr("tui.pipeline.view.spectrum"), tr("tui.pipeline.view.values")};
    const auto current = static_cast<std::size_t>(probe_view_);
    const auto word = [&](std::size_t k) { return k == current ? "[" + words[k] + "]" : " " + words[k] + " "; };
    const int view_width = display_width(word(0)) + display_width(word(1)) + display_width(word(2));
    std::vector<std::string> chips;
    for (const auto& [name, value] : run.probes) chips.push_back(name);
    const auto sweep = model_->sweep_view();
    if (has_sweep())
        chips.push_back(sweep.running ? fmt::format(fmt::runtime(tr("tui.pipeline.sweep.chip_running")), fmt::arg("done", sweep.done), fmt::arg("total", sweep.total))
                                      : tr("tui.pipeline.sweep.chip"));
    const auto chip = [&](std::size_t i) { return (i == probe_index_ ? "[" : " ") + ellipsize(chips[i], 16, ascii) + (i == probe_index_ ? "]" : " "); };
    const int room = std::max(8, width - view_width - 1);
    std::size_t lo = std::min(probe_index_, chips.empty() ? 0 : chips.size() - 1), hi = lo + 1;
    int used = chips.empty() ? 0 : display_width(chip(lo));
    const std::string more = ascii ? "..." : "…";
    while (true) {
        const int marks = (lo > 0 ? 2 : 0) + (hi < chips.size() ? 2 : 0);
        if (hi < chips.size() && used + display_width(chip(hi)) + marks <= room) used += display_width(chip(hi++));
        else if (lo > 0 && used + display_width(chip(lo - 1)) + marks <= room) used += display_width(chip(--lo));
        else break;
    }
    Elements head;
    probe_chip_boxes_.assign(chips.size(), kNoBox);
    if (lo > 0) head.push_back(text(more + " ") | fg(th.dim, ui_));
    for (std::size_t i = lo; i < hi && i < chips.size(); ++i)
        head.push_back(text(chip(i)) | fg(i == probe_index_ ? th.accent : th.fg, ui_) | reflect(probe_chip_boxes_[i]));
    if (hi < chips.size()) head.push_back(text(" " + more) | fg(th.dim, ui_));
    head.push_back(filler());
    for (std::size_t k = 0; k < words.size(); ++k) head.push_back(text(word(k)) | fg(k == current ? th.fg : th.dim, ui_) | reflect(probe_view_boxes_[k]));
    Elements rows;
    if (chips.empty()) {
        rows.push_back(paragraph(tr("tui.pipeline.probe_hint")) | fg(th.dim, ui_));
        return frame_panel(kProbe, vbox(std::move(rows)), {}, tr("tui.pipeline.probe"));
    }
    rows.push_back(hbox(std::move(head)));
    const int plot_rows = std::max(2, height - 3);
    if (probe_index_ >= run.probes.size()) {
        rows.push_back(sweep_body(sweep, width, plot_rows));
        rows.push_back(filler());
        std::string info;
        if (sweep.result) {
            info = tr_count("unit.run", static_cast<long long>(sweep.result->rows.size()));
            const auto failed = std::count_if(sweep.result->rows.begin(), sweep.result->rows.end(), [](const pipeline::SweepRow& row) { return !row.error.empty(); });
            if (failed > 0) info += " · " + fmt::format(fmt::runtime(tr("tui.pipeline.sweep.failed")), fmt::arg("count", failed));
            info += " · ";
        }
        info += tr(sweep.running ? "tui.pipeline.sweep.keys_running" : "tui.pipeline.sweep.keys");
        rows.push_back(text(ellipsize(info, std::max(10, width), ascii)) | fg(th.dim, ui_));
        const auto title = sweep.result ? tr("tui.pipeline.probe") + " · " + tr("tui.pipeline.sweep.chip") + " · " + sweep.result->axis.node + "." + sweep.result->axis.param
                                        : tr("tui.pipeline.probe") + " · " + tr("tui.pipeline.sweep.chip");
        return frame_panel(kProbe, vbox(std::move(rows)), {}, title);
    }
    const auto& value = *run.probes.at(probe_.name.empty() || !run.probes.contains(probe_.name) ? run.probes.begin()->first : probe_.name);
    const auto* audio = std::get_if<core::AudioBuffer>(&value);
    if (probe_.samples && probe_view_ == ProbeView::values && audio) {
        rows.push_back(probe_values(*audio, width, plot_rows));
        rows.push_back(filler());
    } else if (probe_.samples && probe_view_ == ProbeView::time) {
        rows.push_back(probe_time(width, plot_rows));
    } else if (probe_.samples && probe_view_ == ProbeView::spectrum && !probe_.db.empty()) {
        PlotSpec spec;
        spec.x = probe_zoom_.axis("probe_spectrum", {std::max(1.0, probe_.freqs.size() > 1 ? probe_.freqs[1] : 1.0), probe_.rate / 2, AxisScale::log10}).axis();
        double top = -1e300;
        for (const auto& db : probe_.db) top = std::max(top, *std::max_element(db.begin(), db.end()));
        spec.y = {top - 100, top + 3};
        for (std::size_t c = 0; c < probe_.db.size(); ++c) spec.traces.push_back({probe_.db[c], probe_.freqs, th.trace[c % th.trace.size()]});
        const bool legend = probe_.db.size() > 1;
        if (legend) rows.push_back(channel_legend(probe_.db.size(), width, false));
        auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, plot_rows - (legend ? 1 : 0));
        std::vector<std::string> names;
        for (std::size_t c = 0; c < probe_.db.size(); ++c) names.push_back("ch" + std::to_string(c));
        rows.push_back(probe_zoom_.track("probe_spectrum", std::move(spec), std::move(drawn),
                                         {tr("tui.pipeline.probe") + " · " + probe_.name + " · " + tr("tui.pipeline.view.spectrum"), names, tr("tui.export.freq_hz"), tr("tui.filters.db")}));
    } else if (!probe_.samples) {
        Elements lines;
        if (const auto* r = std::get_if<pipeline::Report>(&value)) {
            lines.push_back(text(std::string{glyph(r->passed ? Glyph::check : Glyph::cross, ui_.caps.glyphs)} + " " + tr(r->passed ? "tui.pipeline.report.passed" : "tui.pipeline.report.failed")) |
                            fg(r->passed ? th.ok : th.err, ui_));
            int key_width = 1;
            for (const auto& [key, field] : r->fields) key_width = std::max(key_width, display_width(key));
            key_width = std::min(key_width, std::max(4, width / 2));
            for (const auto& [key, field] : r->fields) {
                const auto shown = std::holds_alternative<double>(field) ? format_number(std::get<double>(field), 4) : std::get<std::string>(field);
                lines.push_back(hbox({text(pad_right(ellipsize(key, key_width, ascii), key_width + 1)) | fg(th.dim, ui_),
                                      text(ellipsize(shown, std::max(4, width - key_width - 1), ascii)) | fg(th.fg, ui_)}));
            }
        } else if (const auto* number = std::get_if<double>(&value)) {
            lines.push_back(text(format_number(*number, 6)) | bold | fg(th.fg, ui_));
        } else if (const auto* bank = std::get_if<core::FilterBank>(&value)) {
            lines.push_back(text(bank->name) | bold | fg(th.fg, ui_));
            for (const auto& [role, h] : {std::pair{"h0", &bank->h0}, std::pair{"h1", &bank->h1}, std::pair{"f0", &bank->f0}, std::pair{"f1", &bank->f1}})
                lines.push_back(hbox({text(pad_right(role, 4)) | fg(th.dim, ui_), text(tr_count("unit.tap", static_cast<long long>(h->coeffs.size()), group(h->coeffs.size()))) | fg(th.fg, ui_)}));
        } else if (const auto* coeffs = std::get_if<pipeline::Coeffs>(&value); coeffs && !coeffs->channels.empty()) {
            std::size_t depth = coeffs->basis.empty() ? coeffs->levels() : 0;
            for (const auto& n : coeffs->basis) depth = std::max(depth, n.level);
            lines.push_back(text(coeffs->transform + " · " + coeffs->wavelet + " · J = " + std::to_string(depth)) | bold | fg(th.fg, ui_));
            const auto& bands = coeffs->channels.front().bands;
            const std::size_t channels = coeffs->channels.size();
            std::vector<double> totals(channels, 0.0);
            for (std::size_t c = 0; c < channels; ++c)
                for (const auto& b : coeffs->channels[c].bands)
                    for (double v : b) totals[c] += v * v;
            int name_width = 4;
            for (std::size_t k = 0; k < bands.size(); ++k) name_width = std::max(name_width, display_width(pipeline::band_label(*coeffs, k)));
            name_width = std::min(name_width, std::max(4, width - 20));
            const auto hidden_note = [&](std::size_t n) { return "  " + fmt::format(fmt::runtime(tr("tui.pipeline.values.more_channels")), fmt::arg("count", n)); };
            std::size_t shown = channels;
            while (shown > 1 && name_width + 10 + 10 * static_cast<int>(shown) + (shown < channels ? display_width(hidden_note(channels - shown)) : 0) > width) --shown;
            if (channels > 1) {
                Elements header{text(std::string(static_cast<std::size_t>(name_width + 10), ' '))};
                for (std::size_t c = 0; c < shown; ++c) header.push_back(text(pad_left("ch" + std::to_string(c), 10)) | fg(th.trace[c % th.trace.size()], ui_));
                if (shown < channels) header.push_back(text(hidden_note(channels - shown)) | fg(th.warn, ui_));
                lines.push_back(hbox(std::move(header)));
            }
            for (std::size_t k = 0; k < bands.size(); ++k) {
                const auto name = ellipsize(pipeline::band_label(*coeffs, k), name_width, ascii);
                Elements row{text(pad_right(name, name_width + 1)) | fg(th.trace[0], ui_), text(pad_left(group(bands[k].size()), 9)) | fg(th.dim, ui_)};
                for (std::size_t c = 0; c < shown; ++c) {
                    double e = 0;
                    if (k < coeffs->channels[c].bands.size())
                        for (double v : coeffs->channels[c].bands[k]) e += v * v;
                    row.push_back(text(pad_left(format_fixed(totals[c] > 0 ? 100 * e / totals[c] : 0, 2) + " %", 10)) | fg(th.fg, ui_));
                }
                lines.push_back(hbox(std::move(row)));
            }
        }
        rows.push_back(scrolled(std::move(lines), probe_text_scroll_, plot_rows, std::nullopt, ui_));
        rows.push_back(filler());
    } else {
        rows.push_back(filler());
    }
    std::string info = std::string{pipeline::to_string(pipeline::type_of(value))};
    if (probe_.samples)
        info += " · " + std::to_string(probe_.samples->size()) + " " + tr("tui.pipeline.channels") + " · " + group(probe_.frames) + " · " + format_si(probe_.rate, tr("unit.hz"));
    if (const auto* g = model_->graph()) {
        const auto p = std::find_if(g->probes.begin(), g->probes.end(), [&](const auto& pr) { return pr.name == probe_.name; });
        if (p != g->probes.end())
            if (const auto it = run.nodes.find(g->nodes[p->source.node].id); it != run.nodes.end() && it->second.status.state == pipeline::NodeState::cached)
                info += " · " + tr("tui.pipeline.cached") + " #" + std::to_string(run.number);
    }
    if (probe_.samples && probe_view_ == ProbeView::values) info += " · " + tr("tui.pipeline.values_keys");
    else if (run.probes.size() > 1) info += " · " + tr("tui.pipeline.probe_keys");
    rows.push_back(text(ellipsize(info, std::max(10, width), ascii)) | fg(th.dim, ui_));
    return frame_panel(kProbe, vbox(std::move(rows)), {}, tr("tui.pipeline.probe") + " · " + probe_.name);
}

ftxui::Element PipelineScreen::text_panel(int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements rows;
    std::istringstream in{pipeline::write_sig(model_->doc())};
    const auto& sel = model_->selection;
    std::optional<std::size_t> chosen_row;
    for (std::string line; std::getline(in, line);) {
        const bool chosen = sel.kind == Selection::Kind::node && line.starts_with("node " + sel.node + " ");
        if (chosen && !chosen_row) chosen_row = rows.size();
        for (const auto& part : wrap_codepoints(line, std::max(8, width), "    ")) {
            auto e = text(part) | fg(chosen ? th.accent : th.dim, ui_);
            rows.push_back(chosen ? e | bold : e);
        }
    }
    const auto key = sel.kind == Selection::Kind::node ? sel.node : std::string{};
    const auto follow = key != text_follow_ ? chosen_row : std::nullopt;
    text_follow_ = key;
    return frame_panel(kText, scrolled(std::move(rows), text_scroll_, std::max(1, height), follow, ui_), {}, tr("tui.pipeline.text"));
}

ftxui::Element PipelineScreen::OnRender() {
    using namespace ftxui;
    panel_boxes_.fill(kNoBox);
    graph_box_ = kNoBox;
    probe_view_boxes_.fill(kNoBox);
    probe_chip_boxes_.clear();
    param_boxes_.clear();
    if (!adding_) add_rows_.clear();
    picture_ = {};
    probe_zoom_.begin_frame();
    if (!seeded_ && current_file_ && model_->doc().nodes.empty() && !model_->path())
        if (const auto file = current_file_()) {
            seeded_ = true;
            const auto id = model_->add_node("wav");
            if (!id.empty()) (void)model_->set_param(id, "path", std::filesystem::absolute(*file).string());
        }
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    const int content = std::max(8, rows - 3);
    if (breakpoint_ <= Breakpoint::small || maximized_) {
        const auto p = adding_ ? kInspector : maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        switch (p) {
        case kGraph: return vbox({toolbar(), graph_panel(cols - 4, content - 3) | flex});
        case kInspector: return vbox({toolbar(), (adding_ ? add_panel(cols - 4, content - 5) : inspector_panel(cols - 4, content - 5)) | flex});
        case kRun: return vbox({toolbar(), run_panel(cols - 4, content - 5) | flex});
        case kProbe: return vbox({toolbar(), probe_panel(cols - 4, content - 3) | flex});
        case kText: return vbox({toolbar(), text_panel(cols - 4, content - 5) | flex});
        case panel_count: break;
        }
    }
    const int graph_w = cols - kRightWidth - 1 - 4, graph_h = content - 1 - kBottomHeight - 2;
    auto bottom = hbox({probe_panel(cols - kRightWidth - 1 - kTextWidth - 1 - 4, kBottomHeight - 2) | flex, text(" "), text_panel(kTextWidth - 4, kBottomHeight - 2) | size(WIDTH, EQUAL, kTextWidth)});
    auto left = vbox({graph_panel(graph_w, graph_h) | flex, bottom | size(HEIGHT, EQUAL, kBottomHeight)});
    auto right = vbox({(adding_ ? add_panel(kRightWidth - 4, content - 2 - kRunHeight) : inspector_panel(kRightWidth - 4, content - 2 - kRunHeight - 2)) | flex, run_panel(kRightWidth - 4, kRunHeight - 2) | size(HEIGHT, EQUAL, kRunHeight)});
    return vbox({toolbar(), hbox({left | flex, text(" "), right | size(WIDTH, EQUAL, kRightWidth)}) | flex});
}

std::optional<Axis> PipelineScreen::plot_view(std::string_view id) const { return probe_zoom_.view(id); }

std::optional<std::string> PipelineScreen::explain_at(int x, int y) const {
    if (adding_) {
        for (const auto& [type, row] : add_rows_)
            if (valid(row) && row.Contain(x, y)) return tr_opt("pipeline.about." + type);
        return std::nullopt;
    }
    if (const auto* n = selected_node())
        for (std::size_t k = 0; k < param_boxes_.size() && k < n->type->params.size(); ++k)
            if (valid(param_boxes_[k]) && param_boxes_[k].Contain(x, y)) {
                const auto& name = n->type->params[k].name;
                const auto about = tr_opt("pipeline.param." + n->type->id + "." + name);
                return about ? std::optional{name + " — " + *about} : std::nullopt;
            }
    if (const auto at = to_picture(x, y); at && model_->graph())
        for (const auto& node : picture_.nodes)
            if (at->first >= node.x && at->first < node.x + node.w && at->second >= node.y && at->second < node.y + node.h)
                for (const auto& gn : model_->graph()->nodes)
                    if (gn.id == node.id) return tr_opt("pipeline.about." + gn.type->id);
    return std::nullopt;
}

std::vector<MenuItem> PipelineScreen::context_menu(int x, int y) {
    if (auto items = probe_zoom_.menu(x, y, ui_); !items.empty()) return items;
    if (valid(graph_box_) && graph_box_.Contain(x, y) && graph_zoom_ != kGraphZoomDefault)
        return {{tr("tui.menu.reset_zoom"), "0", [this] { set_graph_zoom(kGraphZoomDefault, std::nullopt); }}};
    return {};
}

void PipelineScreen::set_graph_zoom(int zoom, std::optional<std::pair<int, int>> anchor) {
    zoom = std::clamp(zoom, 0, kGraphZoomMax);
    if (zoom == graph_zoom_) return;
    const auto before = graph_metrics(graph_zoom_), after = graph_metrics(zoom);
    const double sx = static_cast<double>(after.node_w + after.col_gap) / (before.node_w + before.col_gap);
    const double sy = static_cast<double>(after.node_h + after.row_gap) / (before.node_h + before.row_gap);
    const int ax = anchor ? anchor->first : view_w_ / 2, ay = anchor ? anchor->second : view_h_ / 2;
    ox_ = static_cast<int>(std::lround((ox_ + ax) * sx)) - ax;
    oy_ = static_cast<int>(std::lround((oy_ + ay) * sy)) - ay;
    graph_zoom_ = zoom;
    follow_ = model_->selection.kind == Selection::Kind::node;
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
    if (id == "inspector") return panel_boxes_[kInspector];
    if (id == "text_panel") return panel_boxes_[kText];
    if (id == "probe_time_plot") return probe_zoom_.box("probe_time");
    if (id == "probe_sweep_plot") return probe_zoom_.box("probe_sweep");
    if (id.starts_with("probe_time_plot:")) return probe_zoom_.box("probe_time:" + std::string{id.substr(16)});
    if (id == "probe_panel") return panel_boxes_[kProbe];
    for (std::size_t k = 0; k < probe_view_boxes_.size(); ++k)
        if (id == std::string{"probe_view:"} + std::array{"time", "spectrum", "values"}[k]) return probe_view_boxes_[k];
    if (id.starts_with("probe_chip:")) {
        const auto run = model_->run_view();
        std::size_t k = 0;
        for (const auto& [name, value] : run.probes) {
            if ("probe_chip:" + name == id && k < probe_chip_boxes_.size()) return probe_chip_boxes_[k];
            ++k;
        }
        return kNoBox;
    }
    if (id == "probe_spectrum_plot") return probe_zoom_.box("probe_spectrum");
    if (id == "run_panel") return panel_boxes_[kRun];
    for (const auto& [type, row] : add_rows_)
        if (id == "add:" + type) return row;
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
    if (wheel_scroll(m, panel_boxes_[kText], text_scroll_) || wheel_scroll(m, panel_boxes_[kRun], run_scroll_) || wheel_scroll(m, panel_boxes_[kInspector], inspector_scroll_)) return true;
    if (probe_zoom_.on_mouse(m)) return sync_stacked(), true;
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        for (std::size_t i = 0; i < probe_chip_boxes_.size(); ++i)
            if (valid(probe_chip_boxes_[i]) && probe_chip_boxes_[i].Contain(m.x, m.y)) {
                probe_index_ = i;
                probe_text_scroll_ = {};
                focus_ = kProbe;
                return true;
            }
        for (std::size_t k = 0; k < probe_view_boxes_.size(); ++k)
            if (valid(probe_view_boxes_[k]) && probe_view_boxes_[k].Contain(m.x, m.y)) return probe_view_ = static_cast<ProbeView>(k), focus_ = kProbe, true;
    }
    if ((!probe_.samples || probe_view_ == ProbeView::values) && wheel_scroll(m, panel_boxes_[kProbe], probe_text_scroll_)) return true;
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed && panel_boxes_[kProbe].Contain(m.x, m.y)) probe_zoom_.press(m.x, m.y);
    if ((m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) && m.control && valid(graph_box_) && graph_box_.Contain(m.x, m.y)) {
        set_graph_zoom(graph_zoom_ + (m.button == Mouse::WheelUp ? 1 : -1), std::pair{m.x - graph_box_.x_min, m.y - graph_box_.y_min});
        return true;
    }
    if ((m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) && valid(graph_box_) && graph_box_.Contain(m.x, m.y)) {
        const int step = m.button == Mouse::WheelDown ? 3 : -3;
        if (m.shift) ox_ += 2 * step;
        else oy_ += step;
        clamp_view();
        return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        for (int p = 0; p < panel_count; ++p)
            if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) focus_ = static_cast<Panel>(p);
        if (focus_ == kInspector)
            for (std::size_t k = 0; k < param_boxes_.size(); ++k)
                if (valid(param_boxes_[k]) && param_boxes_[k].Contain(m.x, m.y)) {
                    if (k == param_) edit_param(0);
                    param_ = k;
                    return true;
                }
        const auto at = to_picture(m.x, m.y);
        if (!at) return false;
        const auto [px, py] = *at;
        for (const auto& p : picture_.ports)
            if (p.x == px && p.y == py && p.output) {
                drag_ = {Drag::Kind::wire, p, m.x, m.y, graph_box_};
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
    if (drag_.kind == Drag::Kind::wire && m.motion == Mouse::Moved) {
        drag_.last_x = m.x;
        drag_.last_y = m.y;
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
    if (adding_) return on_add_key(e);
    if (e.is_mouse()) return on_mouse(e.mouse());
    if (e == Event::Character('a')) {
        adding_ = true;
        query_.clear();
        pick_ = 0;
        return true;
    }
    if (e == Event::Character('r')) {
        if (ui_.jobs) model_->run(*ui_.jobs);
        return true;
    }
    if (e == Event::Escape && (model_->run_view().running || model_->sweep_view().running)) {
        if (ui_.jobs) model_->cancel(*ui_.jobs);
        return true;
    }
    if (e == Event::Character('v')) return probe_view_ = static_cast<ProbeView>((static_cast<int>(probe_view_) + 1) % 3), true;
    if (e == Event::Character('n')) return name_probe(), true;
    if (e == Event::Character('k')) return stacked_ = !stacked_, true;
    if (e == Event::Character('S')) return start_sweep(), true;
    if (e == Event::Character('D')) return duplicate_selected(), true;
    if (e == Event::Character('y')) return copy_probe(), true;
    if (e == Event::Character('w')) return toggle_compare(), true;
    if (e == Event::Character('x')) return save_probe(), true;
    if (focus_ == kProbe && probe_view_ == ProbeView::values) {
        if (e == Event::Character('+') || e == Event::Character('=')) return value_digits_ = std::min(15, value_digits_ + 1), true;
        if (e == Event::Character('-')) return value_digits_ = std::max(1, value_digits_ - 1), true;
        if (e == Event::Character('E')) return scientific_ = !scientific_, true;
    }
    if (e == Event::Character('A')) return play(true), true;
    if (e == Event::Character('B')) return play(false), true;
    if (e == Event::Character(' ')) {
        if (ui_.player && ui_.player->state() == audio::PlayState::playing && ui_.player->tag().starts_with("pipeline")) ui_.player->stop();
        else play(last_input_);
        return true;
    }
    const bool table = (probe_.samples || probe_index_ >= model_->run_view().probes.size()) && probe_view_ == ProbeView::values;
    if (focus_ == kProbe && !table && probe_zoom_.on_key(e, probe_view_ == ProbeView::spectrum ? "probe_spectrum" : "probe_time")) return true;
    if (focus_ == kProbe && table && key_scroll(e, probe_text_scroll_)) return true;
    if (focus_ == kProbe && (e == Event::ArrowDown || e == Event::ArrowUp || e == Event::ArrowLeft || e == Event::ArrowRight)) {
        const auto n = model_->run_view().probes.size() + (has_sweep() ? 1 : 0);
        const bool next = e == Event::ArrowDown || e == Event::ArrowRight;
        if (n > 0) probe_index_ = next ? (probe_index_ + 1) % n : (probe_index_ + n - 1) % n;
        probe_text_scroll_ = {};
        return true;
    }
    if (focus_ == kProbe && key_scroll(e, probe_text_scroll_, false)) return true;
    if (e == Event::CtrlZ) return model_->undo(), true;
    if (e == Event::CtrlY) return model_->redo(), true;
    if (e == Event::CtrlS) {
        if (model_->path()) file_result(model_->save(*model_->path()));
        else if (ui_.ask_text) ui_.ask_text(tr("tui.pipeline.save_as"), "", [this](std::string path) { file_result(model_->save(utf8_file(path))); });
        return true;
    }
    if (e == Event::Character('O')) return open_examples(), true;
    if (e == Event::Character('o')) {
        if (ui_.ask_text) ui_.ask_text(tr("tui.pipeline.open_prompt"), "", [this](std::string path) { file_result(model_->load(utf8_file(path))); });
        return true;
    }
    if (e == Event::Character('M')) {
        if (const auto* n = selected_node()) report(model_->toggle_bypass(n->id));
        return true;
    }
    if (e == Event::Character('p')) {
        const auto& sel = model_->selection;
        if (sel.kind == Selection::Kind::wire) model_->toggle_probe(sel.wire.from);
        else if (sel.kind == Selection::Kind::node) model_->toggle_probe({sel.node, {}, 0, 0});
        return true;
    }
    if (e == Event::Delete) {
        const auto sel = model_->selection;
        if (sel.kind == Selection::Kind::node) model_->remove_node(sel.node);
        else if (sel.kind == Selection::Kind::wire) model_->disconnect(sel.wire);
        model_->selection = {};
        return true;
    }
    if (e == Event::Character('c')) {
        if (const auto* n = selected_node(); n && !n->type->outputs.empty()) {
            std::size_t k = 0;
            if (connecting_ && connecting_->node == n->id)
                for (std::size_t i = 0; i < n->type->outputs.size(); ++i)
                    if (n->type->outputs[i].name == connecting_->port || (connecting_->port.empty() && i == 0)) k = (i + 1) % n->type->outputs.size();
            connecting_ = short_ref(n->id, n->type->outputs[k].name, n->type->outputs.size());
            notice_ = fmt::format(fmt::runtime(tr("tui.pipeline.connect_from")), fmt::arg("node", connecting_->port.empty() ? n->id : n->id + "." + connecting_->port));
        }
        return true;
    }
    if (e == Event::Character('C') && ui_.choose) {
        const auto* n = selected_node();
        const auto* g = model_->graph();
        if (!n || !g || n->type->outputs.empty()) return true;
        std::vector<Command> items;
        for (std::size_t o = 0; o < n->type->outputs.size(); ++o) {
            const auto from = short_ref(n->id, n->type->outputs[o].name, n->type->outputs.size());
            const auto from_name = from.port.empty() ? n->id : n->id + "." + from.port;
            for (const auto& m : g->nodes) {
                if (m.id == n->id) continue;
                for (std::size_t i = 0; i < m.type->inputs.size(); ++i) {
                    if (i < m.inputs.size() && m.inputs[i]) continue;
                    if (m.type->inputs[i].type != n->type->outputs[o].type) continue;
                    const auto to = short_ref(m.id, m.type->inputs[i].name, m.type->inputs.size());
                    const auto to_name = to.port.empty() ? m.id : m.id + "." + to.port;
                    items.push_back({from_name + "->" + to_name, from_name + " → " + to_name, [this, from, to] { report(model_->connect(from, to)); }});
                }
            }
        }
        if (items.empty()) notice_ = tr("tui.pipeline.no_free_inputs");
        else ui_.choose(tr("tui.pipeline.connect_title"), std::move(items));
        return true;
    }
    if (connecting_ && e == Event::Escape) {
        connecting_.reset();
        notice_.clear();
        return true;
    }
    if (connecting_ && e == Event::Return) {
        const auto from = *connecting_;
        connecting_.reset();
        notice_.clear();
        if (const auto* n = selected_node(); n && !n->type->inputs.empty()) {
            std::size_t k = 0;
            for (std::size_t i = 0; i < n->inputs.size(); ++i)
                if (!n->inputs[i]) {
                    k = i;
                    break;
                }
            report(model_->connect(from, short_ref(n->id, n->type->inputs[k].name, n->type->inputs.size())));
        }
        return true;
    }
    if (focus_ == kInspector && on_inspector_key(e)) return true;
    if (e == Event::Return && selected_node()) {
        focus_ = kInspector;
        if (breakpoint_ <= Breakpoint::small) layout_.show_panel(kInspector);
        param_ = 0;
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
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
        return true;
    }
    if (e == Event::Escape) {
        if (focus_ == kInspector) {
            focus_ = kGraph;
            if (breakpoint_ <= Breakpoint::small) layout_.show_panel(kGraph);
        }
        else if (maximized_) maximized_.reset();
        else if (model_->selection.kind != Selection::Kind::none) model_->selection = {};
        else return false;
        notice_.clear();
        return true;
    }
    if (focus_ == kGraph && (e == Event::Character('+') || e == Event::Character('='))) return set_graph_zoom(graph_zoom_ + 1, std::nullopt), true;
    if (focus_ == kGraph && e == Event::Character('-')) return set_graph_zoom(graph_zoom_ - 1, std::nullopt), true;
    if (focus_ == kGraph && e == Event::Character('0')) return set_graph_zoom(kGraphZoomDefault, std::nullopt), true;
    if (focus_ == kRun && key_scroll(e, run_scroll_)) return true;
    if (focus_ == kText && key_scroll(e, text_scroll_)) return true;
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

std::shared_ptr<TabScreen> make_pipeline_screen(UiContext& ui, std::shared_ptr<PipelineModel> model,
                                                std::function<std::optional<std::filesystem::path>()> current_file) {
    return std::make_shared<PipelineScreen>(ui, std::move(model), std::move(current_file));
}

}  // namespace signals::tui
