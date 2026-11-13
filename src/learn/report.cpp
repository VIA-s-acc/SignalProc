#include "learn/report.hpp"

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "learn/checker.hpp"

namespace signals::learn {

namespace {

template <class... Args>
std::string say(std::string_view key, Args&&... args) {
    return fmt::format(fmt::runtime(tr(key)), std::forward<Args>(args)...);
}

const TaskProgress* progress_of(const Progress& p, std::string_view id) {
    const auto it = p.tasks.find(id);
    return it == p.tasks.end() ? nullptr : &it->second;
}

bool done(const TaskProgress* t) { return t && t->state != TaskState::open && !t->solved_at.empty(); }

}  // namespace

std::string progress_report(const Course& c, const Progress& p, std::string_view version, std::string_view date, const GraphSource& graph) {
    std::string out = "# " + say("learn.report.title") + "\n\n";
    out += "- " + say("learn.report.student", fmt::arg("name", p.student.empty() ? std::string{"—"} : p.student)) + "\n";
    out += "- " + say("learn.report.variant", fmt::arg("variant", p.variant)) + "\n";
    out += "- " + say("learn.report.version", fmt::arg("version", version), fmt::arg("date", date)) + "\n\n";
    out += "| " + say("learn.report.chapter") + " | " + say("learn.report.solved") + " |\n|---|---|\n";
    std::size_t solved = 0, revealed = 0;
    for (const auto& ch : c.chapters) {
        std::size_t n = 0;
        for (const auto& t : ch.tasks) {
            const auto* tp = progress_of(p, t.id);
            if (!done(tp)) continue;
            ++n;
            revealed += tp->state == TaskState::revealed;
        }
        solved += n;
        out += fmt::format("| {}. {} | {} |\n", ch.id, ch.title(), say("learn.report.of", fmt::arg("n", n), fmt::arg("total", ch.tasks.size())));
    }
    out += "\n" + say("learn.report.total", fmt::arg("n", solved), fmt::arg("total", c.size()), fmt::arg("revealed", revealed)) + "\n";
    std::string open;
    for (const auto& ch : c.chapters)
        for (const auto& t : ch.tasks) {
            const auto* tp = progress_of(p, t.id);
            if (!tp) continue;
            const auto& text = t.text();
            if (!done(tp)) {
                if (tp->attempts > 0 || tp->hints > 0)
                    open += "- " + say("learn.report.open_line", fmt::arg("id", t.id), fmt::arg("title", text.title), fmt::arg("attempts", tp->attempts),
                                       fmt::arg("hints", tp->hints)) +
                            "\n";
                continue;
            }
            out += fmt::format("\n## {} {}\n\n", t.id, text.title);
            const int variant = tp->variant > 0 ? tp->variant : p.variant;
            out += say(tp->state == TaskState::revealed ? "learn.report.revealed_at" : "learn.report.solved_at", fmt::arg("time", tp->solved_at),
                       fmt::arg("variant", variant), fmt::arg("attempts", tp->attempts), fmt::arg("hints", tp->hints)) +
                   "\n";
            if (!tp->answer.empty()) out += "\n" + say("learn.report.answer", fmt::arg("answer", tp->answer)) + "\n";
            if (!text.goals.empty()) {
                out += "\n" + say("learn.report.goals") + "\n\n";
                for (std::size_t i = 0; i < text.goals.size(); ++i) {
                    out += "- ✓ " + render_text(text.goals[i], t, variant);
                    if (i < tp->results.size() && !tp->results[i].empty()) out += " — " + tp->results[i];
                    out += "\n";
                }
            }
            if (graph)
                if (auto g = graph(t.id)) out += "\n" + say("learn.report.graph") + "\n\n```sig\n" + *g + (g->ends_with('\n') ? "" : "\n") + "```\n";
        }
    if (!open.empty()) out += "\n## " + say("learn.report.open") + "\n\n" + open;
    return out;
}

}  // namespace signals::learn
