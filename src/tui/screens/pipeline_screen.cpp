#include "tui/screens/pipeline_screen.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>
#include <thread>

#include <fmt/format.h>

#include "core/spectral.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "pipeline/sig.hpp"
#include "tui/shell/palette.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr int kRightWidth = 44, kBottomHeight = 12, kTextWidth = 46, kRunHeight = 16;

std::filesystem::path utf8_file(const std::string& text) { return expand_path(text); }

pipeline::PortRef short_ref(const std::string& node, const std::string& port, std::size_t ports) { return {node, ports == 1 ? std::string{} : port, 0, 0}; }

}  // namespace

PipelineScreen::PipelineScreen(UiContext& ui, std::shared_ptr<PipelineModel> model, std::function<std::optional<std::filesystem::path>()> current_file)
    : ui_{ui}, model_{std::move(model)}, current_file_{std::move(current_file)} {
    panel_boxes_.fill(kNoBox);
}

std::optional<TheoryLink> PipelineScreen::theory_link() const {
    const auto* n = selected_node();
    if (!n) return std::nullopt;
    static const std::map<std::string, std::string, std::less<>> cards{{"wav", "wav_in"}, {"gen", "generator"}, {"check", "error"}, {"resample", "upsample"}};
    const auto it = cards.find(n->type->id);
    return TheoryLink{TheoryLink::Kind::card, it == cards.end() ? n->type->id : it->second, block_name(n->type->id)};
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

std::vector<Command> PipelineScreen::commands() {
    std::vector<Command> out;
    out.push_back({"pipeline.example", tr("tui.pipeline.cmd.example"), [this] {
                       report(model_->load(std::filesystem::path{ui_.repo_root} / "examples" / "denoise.sig"));
                   }});
    out.push_back({"pipeline.run", tr("tui.pipeline.cmd.run"), [this] {
                       if (ui_.jobs) model_->run(*ui_.jobs);
                   }});
    out.push_back({"pipeline.save", tr("tui.pipeline.cmd.save"), [this] { OnEvent(ftxui::Event::CtrlS); }});
    out.push_back({"pipeline.open", tr("tui.pipeline.cmd.open"), [this] { OnEvent(ftxui::Event::Character('o')); }});
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
            {"v", tr("tui.pipeline.hint.view")},          {"L", tr("tui.pipeline.hint.layout")},   {tr("tui.pipeline.key.drag"), tr("tui.pipeline.hint.connect")}};
}

const pipeline::GraphNode* PipelineScreen::selected_node() const {
    const auto* g = model_->graph();
    if (!g || model_->selection.kind != Selection::Kind::node) return nullptr;
    const auto it = std::find_if(g->nodes.begin(), g->nodes.end(), [&](const auto& n) { return n.id == model_->selection.node; });
    return it == g->nodes.end() ? nullptr : &*it;
}

void PipelineScreen::report(const core::Result<void>& r) { notice_ = r ? std::string{} : r.error().message; }

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
            param_ = std::min(param_, it->type->params.empty() ? 0 : it->type->params.size() - 1);
            for (std::size_t k = 0; k < it->type->params.size(); ++k) {
                const auto& spec = it->type->params[k];
                const auto value = it->params.count(spec.name) ? it->params.at(spec.name) : spec.fallback;
                const bool choice = spec.kind == pipeline::ParamSpec::Kind::choice;
                auto row = hbox({text(pad_right(spec.name, 14)) | fg(th.dim, ui_), text(choice ? "◂ " + value + " ▸" : value) | fg(th.fg, ui_), filler()});
                if (focus_ == kInspector && k == param_) row = row | bg(tint(th.bg, th.accent, 0.3), ui_);
                rows.push_back(std::move(row));
            }
            if (it->bypass) rows.push_back(text(tr("tui.pipeline.state.bypassed")) | fg(th.warn, ui_));
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

ftxui::Element PipelineScreen::add_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    std::vector<Command> all;
    for (const auto& t : pipeline::block_types()) all.push_back({t.id, block_name(t.id), {}});
    const auto found = filter_commands(all, query_);
    pick_ = std::min(pick_, found.empty() ? 0 : found.size() - 1);
    Elements rows{hbox({text("/ " + query_) | fg(th.fg, ui_), text(ui_.caps.glyphs == GlyphMode::ascii ? "_" : "▏") | fg(th.accent, ui_), filler(),
                        text(std::to_string(found.size()) + " / " + std::to_string(all.size())) | fg(th.dim, ui_)}),
                  text(model_->selection.kind == Selection::Kind::wire ? tr("tui.pipeline.add_into_wire") : tr("tui.pipeline.add_free")) | fg(th.dim, ui_)};
    for (std::size_t k = 0; k < found.size(); ++k) {
        auto row = hbox({text((k == pick_ ? "▸ " : "  ") + found[k]->title) | fg(k == pick_ ? th.accent : th.fg, ui_), filler(), text(found[k]->id) | fg(th.dim, ui_)});
        rows.push_back(k == pick_ ? row | focus : row);
    }
    return frame_panel(kInspector, vbox(std::move(rows)) | yframe, {}, tr("tui.pipeline.add_title"));
}

bool PipelineScreen::on_add_key(const ftxui::Event& e) {
    using ftxui::Event;
    std::vector<Command> all;
    for (const auto& t : pipeline::block_types()) all.push_back({t.id, block_name(t.id), {}});
    const auto found = filter_commands(all, query_);
    if (e == Event::Escape) return adding_ = false, true;
    if (e == Event::ArrowDown) return pick_ = found.empty() ? 0 : std::min(pick_ + 1, found.size() - 1), true;
    if (e == Event::ArrowUp) return pick_ = pick_ > 0 ? pick_ - 1 : 0, true;
    if (e == Event::Backspace) {
        while (!query_.empty() && (static_cast<unsigned char>(query_.back()) & 0xC0) == 0x80) query_.pop_back();
        if (!query_.empty()) query_.pop_back();
        pick_ = 0;
        return true;
    }
    if (e == Event::Return) {
        adding_ = false;
        if (found.empty()) return true;
        const auto type = found[std::min(pick_, found.size() - 1)]->id;
        if (model_->selection.kind == Selection::Kind::wire) report(model_->insert_into_wire(model_->selection.wire, type));
        else if (const auto id = model_->add_node(type); !id.empty()) model_->selection = {Selection::Kind::node, id, {}};
        follow_ = true;
        return true;
    }
    if (e.is_character()) {
        query_ += e.character();
        pick_ = 0;
        return true;
    }
    return true;
}

void PipelineScreen::edit_param(int step) {
    const auto* n = selected_node();
    if (!n || n->type->params.empty()) return;
    const auto& spec = n->type->params[std::min(param_, n->type->params.size() - 1)];
    const auto current = n->params.count(spec.name) ? n->params.at(spec.name) : spec.fallback;
    const auto id = n->id;
    if (spec.kind == pipeline::ParamSpec::Kind::choice && !spec.choices.empty()) {
        auto it = std::find(spec.choices.begin(), spec.choices.end(), current);
        auto k = it == spec.choices.end() ? 0 : static_cast<long>(it - spec.choices.begin());
        k = (k + (step == 0 ? 1 : step) + static_cast<long>(spec.choices.size())) % static_cast<long>(spec.choices.size());
        report(model_->set_param(id, spec.name, spec.choices[static_cast<std::size_t>(k)]));
        return;
    }
    if (step != 0 || !ui_.ask_text) return;
    const auto name = spec.name;
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

ftxui::Element PipelineScreen::run_panel() {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto run = model_->run_view();
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    Elements rows;
    if (run.number == 0) {
        rows.push_back(paragraph(tr("tui.pipeline.run_hint")) | fg(th.dim, ui_));
    } else {
        double done = 0;
        for (const auto& [id, n] : run.nodes) done += n.status.fraction;
        done /= static_cast<double>(std::max<std::size_t>(1, run.nodes.size()));
        auto title = fmt::format(fmt::runtime(tr("tui.pipeline.run_number")), fmt::arg("n", run.number));
        if (run.running) title += " · " + format_fixed(100 * done, 0) + " % · " + fmt::format(fmt::runtime(tr("tui.pipeline.threads")), fmt::arg("n", std::thread::hardware_concurrency()));
        else if (run.cancelled) title += " · " + tr("tui.pipeline.cancelled");
        else if (run.error) title += " · " + tr("tui.pipeline.failed");
        else title += " · " + format_si(run.seconds, tr("unit.s"));
        rows.push_back(text(title) | fg(run.error ? th.err : th.fg, ui_));
        if (run.error) rows.push_back(paragraph(*run.error) | fg(th.err, ui_));
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
                rows.push_back(hbox({text(mark + " ") | fg(color, ui_), text(pad_right(st.id, 10)) | fg(th.fg, ui_), text(pad_left(time, 10)) | fg(th.dim, ui_),
                                     text(pad_left(st.bytes ? format_si(static_cast<double>(st.bytes), tr("unit.byte")) : std::string{"—"}, 10)) | fg(th.dim, ui_),
                                     text(it->second.bottleneck ? " !" : "") | fg(th.warn, ui_)}));
            }
    }
    std::vector<std::string> parts{"$ signals run " + (model_->path() ? model_->path()->filename().string() : tr("tui.pipeline.untitled"))};
    for (const auto& set : run.sets) parts.push_back("--set " + set);
    rows.push_back(filler());
    if (run.structure_changed || !model_->path()) {
        rows.push_back(paragraph(tr("tui.pipeline.save_for_cli")) | fg(th.dim, ui_));
        parts.clear();
    }
    std::string line;
    for (const auto& part : parts) {
        if (!line.empty() && display_width(line) + 1 + display_width(part) > kRightWidth - 4) {
            rows.push_back(text(line + " \\") | fg(th.dim, ui_));
            line = "  ";
        }
        line += (line.empty() || line == "  " ? "" : " ") + part;
    }
    if (!line.empty()) rows.push_back(text(line) | fg(th.dim, ui_));
    rows.push_back(hbox({key_label(" r ", ui_), text(" " + tr("tui.pipeline.hint.run") + "  ") | fg(th.dim, ui_), key_label(" Esc ", ui_),
                         text(" " + tr("tui.pipeline.hint.cancel")) | fg(th.dim, ui_)}));
    return frame_panel(kRun, vbox(std::move(rows)), {}, tr("tui.pipeline.run"));
}

void PipelineScreen::refresh_probe(const RunView& run) {
    std::vector<std::string> names;
    for (const auto& [name, value] : run.probes) names.push_back(name);
    if (names.empty()) {
        probe_ = {};
        return;
    }
    probe_index_ = std::min(probe_index_, names.size() - 1);
    const auto& name = names[probe_index_];
    if (probe_.run == run.number && probe_.name == name && (!spectrum_ || !probe_.db.empty() || !probe_.samples)) return;
    if (probe_.run != run.number || probe_.name != name) {
        probe_ = {};
        probe_.run = run.number;
        probe_.name = name;
        const auto* audio = std::get_if<core::AudioBuffer>(run.probes.at(name).get());
        if (!audio || audio->channels.empty()) return;
        auto samples = std::make_shared<std::vector<std::vector<float>>>();
        for (const auto& c : audio->channels) samples->emplace_back(c.begin(), c.end());
        probe_.samples = samples;
        probe_.pyramid = std::make_unique<MinMaxPyramid>(std::span<const float>{samples->front()});
        probe_.rate = audio->sample_rate;
        probe_.frames = audio->frames();
    }
    if (spectrum_ && probe_.db.empty() && probe_.samples) {
        const auto* audio = std::get_if<core::AudioBuffer>(run.probes.at(name).get());
        std::size_t segment = 4096;
        while (segment > 16 && segment > audio->frames()) segment /= 2;
        if (auto psd = core::welch_psd(audio->channels.front(), audio->sample_rate, segment, segment / 2)) {
            probe_.freqs = core::welch_frequencies(audio->sample_rate, segment);
            for (const double v : *psd) probe_.db.push_back(10 * std::log10(std::max(v, 1e-30)));
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

ftxui::Element PipelineScreen::probe_panel(int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto run = model_->run_view();
    refresh_probe(run);
    Elements head;
    std::size_t k = 0;
    for (const auto& [name, value] : run.probes) {
        const bool on = k++ == probe_index_;
        head.push_back(text((on ? "[" : " ") + name + (on ? "]" : " ")) | fg(on ? th.accent : th.fg, ui_));
    }
    head.push_back(filler());
    head.push_back(text(spectrum_ ? " " + tr("tui.pipeline.view.time") + " [" + tr("tui.pipeline.view.spectrum") + "]" : "[" + tr("tui.pipeline.view.time") + "] " + tr("tui.pipeline.view.spectrum") + " ") |
                   fg(th.dim, ui_));
    Elements rows;
    if (run.probes.empty()) {
        rows.push_back(paragraph(tr("tui.pipeline.probe_hint")) | fg(th.dim, ui_));
        return frame_panel(kProbe, vbox(std::move(rows)), {}, tr("tui.pipeline.probe"));
    }
    rows.push_back(hbox(std::move(head)));
    const int plot_rows = std::max(2, height - 3);
    const auto& value = *run.probes.at(probe_.name.empty() || !run.probes.contains(probe_.name) ? run.probes.begin()->first : probe_.name);
    if (probe_.samples && !spectrum_) {
        PlotSpec spec;
        spec.x = {0, static_cast<double>(probe_.frames) / std::max(1.0, probe_.rate)};
        const auto all = probe_.pyramid->query(0, probe_.pyramid->size(), 1);
        const double m = all.empty() ? 1.0 : std::max({1e-12, std::abs(static_cast<double>(all[0].lo)), std::abs(static_cast<double>(all[0].hi))});
        spec.y = {-m, m};
        spec.playhead = playhead();
        const auto* pyramid = probe_.pyramid.get();
        spec.envelopes.push_back(Envelope{{}, th.trace[0], [pyramid](int cols) { return pyramid->query(0, pyramid->size(), static_cast<std::size_t>(cols)); }});
        rows.push_back(plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, plot_rows));
    } else if (probe_.samples && !probe_.db.empty()) {
        PlotSpec spec;
        spec.x = {std::max(1.0, probe_.freqs.size() > 1 ? probe_.freqs[1] : 1.0), probe_.rate / 2, AxisScale::log10};
        const double top = *std::max_element(probe_.db.begin(), probe_.db.end());
        spec.y = {top - 100, top + 3};
        spec.traces.push_back({probe_.db, probe_.freqs, th.trace[0]});
        rows.push_back(plot(std::move(spec), th, ui_.caps.color, ui_.caps.glyphs) | size(HEIGHT, EQUAL, plot_rows));
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
    rows.push_back(text(info) | fg(th.dim, ui_));
    return frame_panel(kProbe, vbox(std::move(rows)), {}, tr("tui.pipeline.probe") + " · " + probe_.name);
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
        case kInspector: return vbox({toolbar(), (adding_ ? add_panel() : inspector_panel()) | flex});
        case kRun: return vbox({toolbar(), run_panel() | flex});
        case kProbe: return vbox({toolbar(), probe_panel(content - 3) | flex});
        case kText: return vbox({toolbar(), text_panel(content - 3) | flex});
        case panel_count: break;
        }
    }
    const int graph_w = cols - kRightWidth - 1 - 4, graph_h = content - 1 - kBottomHeight - 2;
    auto bottom = hbox({probe_panel(kBottomHeight - 2) | flex, text(" "), text_panel(kBottomHeight - 2) | size(WIDTH, EQUAL, kTextWidth)});
    auto left = vbox({graph_panel(graph_w, graph_h) | flex, bottom | size(HEIGHT, EQUAL, kBottomHeight)});
    auto right = vbox({(adding_ ? add_panel() : inspector_panel()) | flex, run_panel() | size(HEIGHT, EQUAL, kRunHeight)});
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
    if (e == Event::Escape && model_->run_view().running) {
        if (ui_.jobs) model_->cancel(*ui_.jobs);
        return true;
    }
    if (e == Event::Character('v')) return spectrum_ = !spectrum_, true;
    if (e == Event::Character('A')) return play(true), true;
    if (e == Event::Character('B')) return play(false), true;
    if (e == Event::Character(' ')) {
        if (ui_.player && ui_.player->state() == audio::PlayState::playing && ui_.player->tag().starts_with("pipeline")) ui_.player->stop();
        else play(last_input_);
        return true;
    }
    if (focus_ == kProbe && (e == Event::ArrowDown || e == Event::ArrowUp)) {
        const auto n = model_->run_view().probes.size();
        if (n > 0) probe_index_ = e == Event::ArrowDown ? (probe_index_ + 1) % n : (probe_index_ + n - 1) % n;
        return true;
    }
    if (e == Event::CtrlZ) return model_->undo(), true;
    if (e == Event::CtrlY) return model_->redo(), true;
    if (e == Event::CtrlS) {
        if (model_->path()) report(model_->save(*model_->path()));
        else if (ui_.ask_text) ui_.ask_text(tr("tui.pipeline.save_as"), "", [this](std::string path) { report(model_->save(utf8_file(path))); });
        return true;
    }
    if (e == Event::Character('o')) {
        if (ui_.ask_text) ui_.ask_text(tr("tui.pipeline.open_prompt"), "", [this](std::string path) { report(model_->load(utf8_file(path))); });
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
