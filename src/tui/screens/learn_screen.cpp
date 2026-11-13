#include "tui/screens/learn_screen.hpp"

#include <algorithm>
#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "learn/checker.hpp"
#include "learn/progress.hpp"
#include "learn/task.hpp"
#include "tui/data/learn_state.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {

namespace {

template <class... Args>
std::string say(std::string_view key, Args&&... args) {
    return fmt::format(fmt::runtime(tr(key)), std::forward<Args>(args)...);
}

learn::TaskState state_of(const learn::Progress& p, const std::string& id) {
    const auto it = p.tasks.find(id);
    return it == p.tasks.end() ? learn::TaskState::open : it->second.state;
}

int hints_of(const learn::Progress& p, const std::string& id) {
    const auto it = p.tasks.find(id);
    return it == p.tasks.end() ? 0 : it->second.hints;
}

std::string mark(learn::TaskState s, bool ascii) {
    switch (s) {
    case learn::TaskState::solved: return ascii ? "+" : "✓";
    case learn::TaskState::revealed: return ascii ? "~" : "◐";
    case learn::TaskState::open: break;
    }
    return ascii ? "." : "·";
}

}  // namespace

LearnScreen::LearnScreen(UiContext& ui) : ui_{ui} {
    if (ui_.learn) {
        const auto view = ui_.learn->view();
        cursor_ = view.progress.active;
        if (!ui_.learn->course().find(cursor_)) {
            cursor_.clear();
            for (const auto& ch : ui_.learn->course().chapters)
                if (!ch.tasks.empty()) {
                    cursor_ = ch.tasks.front().id;
                    break;
                }
        }
    }
}

std::string LearnScreen::tab_label() const { return tr("tui.sub.tasks"); }

std::vector<Hint> LearnScreen::hints() const {
    if (typing_) return {{"Enter", tr("tui.learn.hint.submit")}, {"Esc", tr("tui.learn.hint.cancel")}};
    std::vector<Hint> out{{"↑↓", tr("tui.learn.hint.task")}, {"Enter", tr("tui.learn.hint.start")}, {"F2", tr("tui.learn.hint.check")},
                          {"h", tr("tui.learn.hint.hint")},  {"s", tr("tui.learn.hint.solution")}};
    out.push_back({"a", tr("tui.learn.hint.answer")});
    out.push_back({"t", tr("tui.learn.hint.theory")});
    out.push_back({"v", tr("tui.learn.hint.variant")});
    out.push_back({"Tab", tr("tui.explain.hint.tab")});
    out.push_back({"z", tr("tui.signal.hint.zoom")});
    return out;
}

const learn::Task* LearnScreen::task() const { return ui_.learn ? ui_.learn->course().find(cursor_) : nullptr; }

std::vector<LearnScreen::Row> LearnScreen::rows() const {
    std::vector<Row> out;
    if (!ui_.learn) return out;
    for (const auto& ch : ui_.learn->course().chapters) {
        out.push_back({&ch, nullptr});
        for (const auto& t : ch.tasks) out.push_back({&ch, &t});
    }
    return out;
}

void LearnScreen::move(int delta) {
    std::vector<const learn::Task*> tasks;
    for (const auto& r : rows())
        if (r.task) tasks.push_back(r.task);
    if (tasks.empty()) return;
    auto it = std::ranges::find_if(tasks, [&](const auto* t) { return t->id == cursor_; });
    long long k = it == tasks.end() ? 0 : it - tasks.begin();
    k = std::clamp<long long>(k + delta, 0, static_cast<long long>(tasks.size()) - 1);
    cursor_ = tasks[static_cast<std::size_t>(k)]->id;
    scroll_[text_panel] = scroll_[goals_panel] = scroll_[hints_panel] = 0;
}

void LearnScreen::start() {
    if (!ui_.learn || !task()) return;
    ui_.learn->select(cursor_);
    ui_.learn->start(ui_);
}

void LearnScreen::hint() {
    if (!ui_.learn || !task()) return;
    if (ui_.learn->view().progress.active != cursor_) ui_.learn->select(cursor_);
    if (!ui_.learn->next_hint() && ui_.notice) ui_.notice(tr("tui.learn.no_more_hints"));
    focus_ = maximized_ ? focus_ : hints_panel;
}

void LearnScreen::solution() {
    if (!ui_.learn || !task()) return;
    const auto state = state_of(ui_.learn->view().progress, cursor_);
    if (state != learn::TaskState::open) {
        focus_ = hints_panel;
        return;
    }
    const auto id = cursor_;
    auto reveal = [this, id] {
        ui_.learn->select(id);
        ui_.learn->reveal();
        focus_ = hints_panel;
    };
    if (ui_.choose)
        ui_.choose(tr("tui.learn.confirm_title"), {{"learn.reveal", tr("tui.learn.confirm_yes"), reveal}, {"learn.keep", tr("tui.learn.confirm_no"), [] {}}});
}

void LearnScreen::answer() {
    const auto* t = task();
    if (!t || t->kind != learn::TaskKind::answer || !ui_.learn) return;
    if (ui_.learn->view().progress.active != cursor_) ui_.learn->select(cursor_);
    typing_ = true;
    answer_.clear();
}

void LearnScreen::submit() {
    typing_ = false;
    if (ui_.learn && !answer_.empty()) ui_.learn->submit_answer(ui_, answer_);
}

void LearnScreen::open_theory() const {
    const auto link = theory_link();
    if (link && ui_.open_theory) ui_.open_theory(TheoryPage{*link});
}

std::optional<TheoryLink> LearnScreen::theory_link() const {
    const auto* t = task();
    if (!t || t->theory.empty()) return TheoryLink{TheoryLink::Kind::section, "learn"};
    return TheoryLink{TheoryLink::Kind::section, t->theory.front()};
}

void LearnScreen::open_with(const NavArgs& args) {
    if (args.task.empty() || !ui_.learn || !ui_.learn->course().find(args.task)) return;
    const bool same = typing_ && cursor_ == args.task;
    cursor_ = args.task;
    focus_ = text_panel;
    if (args.step && *args.step == 1) {
        if (same) submit();
        else answer();
    }
}

const LearnScreen::Texts& LearnScreen::texts() {
    const auto* t = task();
    if (!t || !ui_.learn) {
        texts_ = {};
        return texts_;
    }
    const auto view = ui_.learn->view();
    const int variant = view.progress.variant;
    const auto opened = static_cast<std::size_t>(hints_of(view.progress, t->id));
    const auto state = state_of(view.progress, t->id);
    const auto key = fmt::format("{}|{}|{}|{}|{}", t->id, variant, opened, static_cast<int>(state), language_code(language()));
    if (texts_.key == key) return texts_;
    const auto& text = t->text();
    Texts out;
    out.key = key;
    std::string md = learn::render_text(text.statement, *t, variant);
    if (const auto options = learn::options_markdown(*t, variant); !options.empty()) md += "\n\n" + options;
    if (!t->theory.empty()) {
        md += "\n\n" + tr("tui.learn.theory") + " ";
        for (std::size_t i = 0; i < t->theory.size(); ++i) md += (i ? ", " : "") + std::string{"{#"} + t->theory[i] + "}";
    }
    out.statement = theory::parse_markdown(md);
    for (const auto& g : text.goals) out.goals.push_back(learn::render_text(g, *t, variant));
    std::string hints;
    const auto shown = std::min(opened, text.hints.size());
    for (std::size_t i = 0; i < shown; ++i) hints += fmt::format("{}. {}\n", i + 1, learn::render_text(text.hints[i], *t, variant));
    if (shown < text.hints.size()) hints += "\n" + say("tui.learn.more_hints", fmt::arg("open", shown), fmt::arg("total", text.hints.size())) + "\n";
    else if (!text.hints.empty()) hints += "\n" + tr("tui.learn.no_more_hints") + "\n";
    if (state != learn::TaskState::open) hints += "\n### " + tr("tui.learn.solution") + "\n\n" + learn::render_text(text.solution, *t, variant) + "\n";
    else hints += "\n" + tr("tui.learn.solution_key") + "\n";
    out.hints = theory::parse_markdown(hints);
    texts_ = std::move(out);
    return texts_;
}

ftxui::Element LearnScreen::clickable(std::string id, ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({std::move(id), kNoBox, std::move(action)});
    return e | ftxui::reflect(clickables_.back().box);
}

std::string LearnScreen::panel_title(Panel p) const {
    const auto* t = task();
    switch (p) {
    case list_panel: {
        if (!ui_.learn) return tr("tui.sub.tasks");
        const auto v = ui_.learn->view();
        std::size_t solved = 0;
        for (const auto& [id, tp] : v.progress.tasks) solved += tp.state != learn::TaskState::open && ui_.learn->course().find(id);
        return say("tui.learn.list_title", fmt::arg("solved", solved), fmt::arg("total", ui_.learn->course().size()),
                   fmt::arg("variant", v.progress.variant));
    }
    case text_panel: return t ? t->id + " " + t->text().title : tr("tui.sub.tasks");
    case goals_panel: return tr(t && t->kind == learn::TaskKind::answer ? "tui.learn.answer_title" : "tui.learn.goals_title");
    case hints_panel: return tr("tui.learn.hints_title");
    case panel_count: break;
    }
    return {};
}

ftxui::Element LearnScreen::frame(Panel p, std::string title, std::vector<Chip> chips, ftxui::Element body) {
    return panel(title, std::move(chips), std::move(body), focus_ == p && !typing_, ui_, notes_.box("panel." + std::to_string(p))) |
           ftxui::reflect(boxes_[p]);
}

ftxui::Element LearnScreen::document_body(const std::vector<theory::Block>& blocks, int width, int height, Panel p) {
    view_height_[p] = std::max(1, height);
    auto& layout = layouts_[p];
    DocumentSpec spec{blocks, std::max(10, width), scroll_[p], 0, ui_.caps.glyphs == GlyphMode::ascii ? theory::MathGlyphs::ascii : theory::MathGlyphs::unicode,
                      std::nullopt, nullptr, {}, view_height_[p]};
    auto e = document(spec, ui_, &layout);
    doc_height_[p] = layout.height;
    if (scroll_[p] > std::max(0, layout.height - view_height_[p])) {
        scroll_[p] = std::max(0, layout.height - view_height_[p]);
        spec.scroll = scroll_[p];
        e = document(spec, ui_, &layout);
    }
    return e;
}

ftxui::Element LearnScreen::list_body(int width, int height) {
    using namespace ftxui;
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const auto all = rows();
    if (all.empty()) return text(tr("tui.learn.empty")) | fg(ui_.theme.dim, ui_);
    const auto view = ui_.learn->view();
    std::optional<std::size_t> follow;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].task && all[i].task->id == cursor_) follow = i;
    const auto window = list_window(list_scroll_.first, all.size(), std::max(1, height), 1, follow);
    Elements shown;
    for (std::size_t i = window.first; i < window.first + window.shown; ++i) {
        const auto& r = all[i];
        if (!r.task) {
            std::size_t solved = 0;
            for (const auto& t : r.chapter->tasks) solved += state_of(view.progress, t.id) != learn::TaskState::open;
            const auto label = fmt::format("{}. {}", r.chapter->id, r.chapter->title());
            const auto count = fmt::format(" {}/{}", solved, r.chapter->tasks.size());
            shown.push_back(hbox({text(ellipsize(label, std::max(4, width - display_width(count)), ascii)) | bold, filler(),
                                  text(count) | fg(ui_.theme.dim, ui_)}));
            continue;
        }
        const auto& t = *r.task;
        const auto state = state_of(view.progress, t.id);
        const auto lead = std::string{t.id == view.progress.active ? (ascii ? "> " : "● ") : "  "} + mark(state, ascii) + " ";
        auto row = text(ellipsize(lead + t.id + " " + t.text().title, width, ascii));
        if (state == learn::TaskState::solved) row |= fg(ui_.theme.ok, ui_);
        if (t.id == cursor_) row = row | inverted;
        row_boxes_.push_back({kNoBox, t.id});
        shown.push_back(row | reflect(row_boxes_.back().first));
    }
    return scrolled(window, all.size(), std::move(shown), ui_);
}

ftxui::Element LearnScreen::text_body(int width, int height) {
    using namespace ftxui;
    const auto* t = task();
    if (!t) return text(tr("tui.learn.empty")) | fg(ui_.theme.dim, ui_);
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    Elements extra;
    if (t->kind == learn::TaskKind::answer) {
        const auto view = ui_.learn->view();
        const auto it = view.progress.tasks.find(t->id);
        const auto last = it == view.progress.tasks.end() ? std::string{} : it->second.answer;
        const auto shown = typing_ ? answer_ + (ascii ? "_" : "▏") : last.empty() ? tr("tui.learn.no_answer") : last;
        extra.push_back(clickable("answer", hbox({text(tr("tui.learn.answer") + " ") | bold, text(ellipsize(shown, std::max(4, width - 10), ascii)) | (typing_ ? inverted : nothing)}),
                                  [this] { answer(); }));
    }
    const int doc_height = std::max(1, height - static_cast<int>(extra.size()) - (extra.empty() ? 0 : 1));
    auto doc = document_body(texts().statement, width, doc_height, text_panel);
    if (extra.empty()) return doc;
    Elements all{doc | flex, separatorEmpty()};
    for (auto& e : extra) all.push_back(std::move(e));
    return vbox(std::move(all));
}

ftxui::Element LearnScreen::goals_body(int width, int height) {
    const auto* t = task();
    if (!t || !ui_.learn) return ftxui::text("");
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const auto view = ui_.learn->view();
    const bool mine = view.progress.active == t->id;
    std::string md;
    if (mine && view.checking) md += tr("tui.learn.checking") + "\n\n";
    else if (mine && view.message) md += *view.message + "\n\n";
    const auto& last = view.last;
    if (mine && last) {
        if (last->error) md += say("tui.learn.error", fmt::arg("error", *last->error)) + "\n\n";
        md += tr(last->passed ? "tui.learn.passed" : "tui.learn.failed") + "\n\n";
    } else if (state_of(view.progress, t->id) == learn::TaskState::open) {
        md += tr(t->kind == learn::TaskKind::answer ? "tui.learn.not_checked_answer" : "tui.learn.not_checked") + "\n\n";
    } else {
        const auto& tp = view.progress.tasks.at(t->id);
        md += say("tui.learn.solved_at", fmt::arg("time", tp.solved_at.empty() ? std::string{"—"} : tp.solved_at)) + "\n\n";
    }
    const auto& goals = texts().goals;
    for (std::size_t i = 0; i < goals.size(); ++i) {
        std::string m = ascii ? "." : "·";
        std::string detail;
        if (mine && last && i < last->goals.size() && !(last->error && !last->goals[i].passed && last->goals[i].detail.empty())) {
            m = last->goals[i].passed ? (ascii ? "+" : "✓") : (ascii ? "x" : "✗");
            detail = last->goals[i].detail;
        }
        md += "- " + m + " " + goals[i] + (detail.empty() ? "" : " — " + detail) + "\n";
    }
    if (t->kind == learn::TaskKind::answer && mine && last && !last->goals.empty() && !last->goals.front().detail.empty())
        md += "- " + std::string{last->passed ? (ascii ? "+" : "✓") : (ascii ? "x" : "✗")} + " " + last->goals.front().detail + "\n";
    return document_body(theory::parse_markdown(md), width, height, goals_panel);
}

ftxui::Element LearnScreen::hints_body(int width, int height) { return document_body(texts().hints, width, height, hints_panel); }

ftxui::Element LearnScreen::render_panel(Panel p, int width, int height) {
    const int inner_w = std::max(10, width - 4);
    const int inner_h = std::max(1, height - 2);
    std::vector<Chip> chips;
    ftxui::Element body;
    switch (p) {
    case list_panel: body = list_body(inner_w, inner_h); break;
    case text_panel: body = text_body(inner_w, inner_h); break;
    case goals_panel: body = goals_body(inner_w, inner_h); break;
    case hints_panel: body = hints_body(inner_w, inner_h); break;
    case panel_count: break;
    }
    return frame(p, panel_title(p), std::move(chips), std::move(body));
}

ftxui::Element LearnScreen::OnRender() {
    using namespace ftxui;
    clickables_.clear();
    row_boxes_.clear();
    notes_.clear();
    boxes_.fill(kNoBox);
    const auto [cols, rows_total] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    const int height = std::max(6, rows_total - 3);
    if (!ui_.learn) return text(tr("tui.learn.unavailable")) | fg(ui_.theme.dim, ui_);
    if (maximized_) {
        focus_ = *maximized_;
        return render_panel(*maximized_, cols, height) | flex;
    }
    if (breakpoint_ <= Breakpoint::small) {
        const int list_h = std::min(8, height / 3);
        const auto shown = focus_ == list_panel ? text_panel : focus_;
        return vbox({render_panel(list_panel, cols, list_h) | size(HEIGHT, EQUAL, list_h), render_panel(shown, cols, height - list_h) | flex});
    }
    const int list_w = std::clamp(cols / 4, 28, 40);
    const int right_w = cols - list_w - 1;
    const int bottom_h = std::max(8, height * 2 / 5);
    const int top_h = height - bottom_h;
    const bool side = right_w >= 90;
    Element bottom = side ? hbox({render_panel(goals_panel, right_w / 2, bottom_h) | size(WIDTH, EQUAL, right_w / 2), text(" "),
                                  render_panel(hints_panel, right_w - right_w / 2 - 1, bottom_h) | flex})
                          : vbox({render_panel(goals_panel, right_w, bottom_h / 2) | size(HEIGHT, EQUAL, bottom_h / 2),
                                  render_panel(hints_panel, right_w, bottom_h - bottom_h / 2) | flex});
    auto right = vbox({render_panel(text_panel, right_w, top_h) | size(HEIGHT, EQUAL, top_h), std::move(bottom) | size(HEIGHT, EQUAL, bottom_h)});
    return hbox({render_panel(list_panel, list_w, height) | size(WIDTH, EQUAL, list_w), text(" "), std::move(right) | flex});
}

bool LearnScreen::scroll(Panel p, int delta) {
    if (p == list_panel) {
        move(delta);
        return true;
    }
    scroll_[p] = std::clamp(scroll_[p] + delta, 0, std::max(0, doc_height_[p] - view_height_[p]));
    return true;
}

bool LearnScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (typing_) {
        if (e == Event::Return) submit();
        else if (e == Event::Escape) typing_ = false;
        else if (e == Event::Backspace) {
            auto cps = split_codepoints(answer_);
            if (!cps.empty()) cps.pop_back();
            answer_.clear();
            for (const auto& c : cps) answer_ += c;
        } else if (e.is_character()) answer_ += e.character();
        return true;
    }
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        if (m.button == ftxui::Mouse::WheelDown || m.button == ftxui::Mouse::WheelUp) {
            const int d = m.button == ftxui::Mouse::WheelDown ? 3 : -3;
            for (int p = 0; p < panel_count; ++p)
                if (valid(boxes_[p]) && boxes_[p].Contain(m.x, m.y)) return scroll(static_cast<Panel>(p), p == list_panel ? d / 3 : d);
            return false;
        }
        if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed) {
            for (const auto& c : clickables_)
                if (valid(c.box) && c.box.Contain(m.x, m.y)) {
                    c.action();
                    return true;
                }
            for (const auto& [box, id] : row_boxes_)
                if (valid(box) && box.Contain(m.x, m.y)) {
                    cursor_ = id;
                    focus_ = list_panel;
                    return true;
                }
            for (int p = 0; p < panel_count; ++p)
                if (valid(boxes_[p]) && boxes_[p].Contain(m.x, m.y)) {
                    focus_ = static_cast<Panel>(p);
                    return true;
                }
        }
        return false;
    }
    if (e == Event::Escape && maximized_) {
        maximized_.reset();
        return true;
    }
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse) {
        const bool back = e == Event::TabReverse;
        focus_ = static_cast<Panel>((focus_ + (back ? panel_count - 1 : 1)) % panel_count);
        if (maximized_) maximized_ = focus_;
        return true;
    }
    if (e == Event::ArrowDown) return scroll(focus_, 1);
    if (e == Event::ArrowUp) return scroll(focus_, -1);
    if (e == Event::PageDown) return scroll(focus_, focus_ == list_panel ? 5 : std::max(1, view_height_[focus_] - 1));
    if (e == Event::PageUp) return scroll(focus_, focus_ == list_panel ? -5 : -std::max(1, view_height_[focus_] - 1));
    if (e == Event::Return) {
        start();
        return true;
    }
    if (e == Event::Character('h')) return hint(), true;
    if (e == Event::Character('s')) return solution(), true;
    if (e == Event::Character('a')) return answer(), true;
    if (e == Event::Character('t')) return open_theory(), true;
    if (e == Event::Character('v')) {
        if (ui_.ask_text && ui_.learn)
            ui_.ask_text(tr("tui.learn.variant_prompt"), std::to_string(ui_.learn->variant()), [this](std::string text) {
                try {
                    const int v = std::stoi(text);
                    if (v >= 1 && v <= 999) ui_.learn->set_variant(v);
                    else if (ui_.notice) ui_.notice(tr("tui.learn.bad_variant"));
                } catch (...) {
                    if (ui_.notice) ui_.notice(tr("tui.learn.bad_variant"));
                }
            });
        return true;
    }
    return false;
}

std::vector<Command> LearnScreen::commands() {
    std::vector<Command> out;
    if (!ui_.learn) return out;
    out.push_back({"learn.start", tr("tui.learn.cmd.start"), [this] { start(); }});
    out.push_back({"learn.check", tr("tui.learn.cmd.check"), [this] {
                       ui_.learn->select(cursor_);
                       if (const auto* t = task(); t && t->kind == learn::TaskKind::answer) answer();
                       else ui_.learn->check(ui_);
                   }});
    out.push_back({"learn.hint", tr("tui.learn.cmd.hint"), [this] { hint(); }});
    out.push_back({"learn.solution", tr("tui.learn.cmd.solution"), [this] { solution(); }});
    out.push_back({"learn.variant", tr("tui.learn.cmd.variant"), [this] { OnEvent(ftxui::Event::Character('v')); }});
    out.push_back({"learn.report", tr("tui.learn.cmd.report"), [this] {
                       if (!ui_.ask_text) return;
                       ui_.ask_text(tr("tui.learn.report_prompt"), "report.md", [this](std::string path) {
                           auto r = ui_.learn->write_report(path);
                           if (ui_.notice) ui_.notice(r ? say("tui.learn.report_written", fmt::arg("file", r->string())) : r.error().message);
                       });
                   }});
    return out;
}

std::vector<JumpTarget> LearnScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& c : clickables_) out.push_back({c.box, c.id});
    return out;
}

std::optional<std::string> LearnScreen::explain_at(int x, int y) const {
    for (const auto& c : clickables_)
        if (valid(c.box) && c.box.Contain(x, y)) return notes_.explain(c.id);
    for (const auto& [box, id] : row_boxes_)
        if (valid(box) && box.Contain(x, y)) return notes_.explain("row");
    return notes_.at(x, y);
}

std::shared_ptr<TabScreen> make_learn_screen(UiContext& ui) { return std::make_shared<LearnScreen>(ui); }

}  // namespace signals::tui
