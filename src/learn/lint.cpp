#include "learn/lint.hpp"

#include <fmt/format.h>

#include "core/expr.hpp"
#include "i18n/i18n.hpp"
#include "learn/answer.hpp"
#include "learn/checker.hpp"
#include "theory/cards.hpp"
#include "theory/markdown.hpp"
#include "theory/sections.hpp"

namespace signals::learn {

namespace {

template <class... Args>
std::string say(std::string_view key, Args&&... args) {
    return fmt::format(fmt::runtime(tr(key)), std::forward<Args>(args)...);
}

std::string goals_summary(const CheckResult& r) {
    if (r.error) return *r.error;
    std::string out;
    for (std::size_t i = 0; i < r.goals.size(); ++i)
        if (!r.goals[i].passed) out += (out.empty() ? "" : "; ") + fmt::format("#{} {}", i + 1, r.goals[i].detail);
    return out;
}

std::string all_text(const TaskText& t) {
    std::string out = t.title + "\n" + t.statement + "\n" + t.solution + "\n";
    for (const auto& g : t.goals) out += g + "\n";
    for (const auto& h : t.hints) out += h + "\n";
    return out;
}

void lint_graph(const Task& t, int variant, std::vector<LintIssue>& out) {
    core::CancelToken cancel;
    const auto run = [&](const std::string& sig) { return check_task(t, variant, {.sig = sig, .base_dir = "."}, cancel); };
    auto good = run(solution_graph(t, variant));
    if (!good) out.push_back({t.id, say("learn.lint.task_error", fmt::arg("variant", variant), fmt::arg("message", good.error().message))});
    else if (!good->passed)
        out.push_back({t.id, say("learn.lint.solution_fails", fmt::arg("variant", variant), fmt::arg("why", goals_summary(*good)))});
    if (auto start = run(start_graph(t, variant)); start && start->passed)
        out.push_back({t.id, say("learn.lint.start_passes", fmt::arg("variant", variant))});
    const auto params = draw_params(t.params, t.id, variant);
    for (const auto& [name, text] : t.cheats)
        if (auto cheat = run(substitute(text, params)); cheat && cheat->passed)
            out.push_back({t.id, say("learn.lint.cheat_passes", fmt::arg("variant", variant), fmt::arg("name", name))});
}

void lint_answer(const Task& t, int variant, std::vector<LintIssue>& out) {
    auto expected = expected_answer(t, variant);
    if (!expected) {
        out.push_back({t.id, say("learn.lint.task_error", fmt::arg("variant", variant), fmt::arg("message", expected.error().message))});
        return;
    }
    core::CancelToken cancel;
    auto r = check_task(t, variant, {.answer = format_answer(*expected, variant_answer(t, variant))}, cancel);
    if (!r || !r->passed)
        out.push_back({t.id, say("learn.lint.solution_fails", fmt::arg("variant", variant), fmt::arg("why", r ? goals_summary(*r) : r.error().message))});
    if (t.answer->type == AnswerType::choice && t.answer->options.size() < 2) out.push_back({t.id, say("learn.lint.few_options")});
    if (!t.answer->verify.empty()) {
        auto measures = reference_measures(t, variant);
        auto e = core::parse_expr(t.answer->verify);
        if (!measures || !e) {
            out.push_back({t.id, say("learn.lint.verify_false", fmt::arg("variant", variant))});
            return;
        }
        const auto params = draw_params(t.params, t.id, variant);
        auto v = core::eval(*e, [&](std::string_view name) -> std::optional<double> {
            if (const auto it = measures->find(name); it != measures->end()) return it->second.scalar();
            if (const auto it = params.find(name); it != params.end())
                if (auto n = core::eval_number(it->second)) return *n;
            return std::nullopt;
        });
        if (!v || *v == 0) out.push_back({t.id, say("learn.lint.verify_false", fmt::arg("variant", variant))});
    }
}

}  // namespace

std::vector<LintIssue> lint_task(const Task& t, std::span<const int> variants) {
    std::vector<LintIssue> out;
    for (const auto& id : t.theory)
        if (!theory::find_section(id) && !theory::find_card(id)) out.push_back({t.id, say("learn.lint.no_section", fmt::arg("id", id))});
    for (const auto* text : {&t.ru, &t.en}) {
        if (!placeholders(text->title).empty()) out.push_back({t.id, say("learn.lint.title_placeholder")});
        if (text->title.empty() || text->statement.empty() || text->solution.empty() || text->hints.empty())
            out.push_back({t.id, say("learn.lint.text_parts")});
        for (const auto& ref : theory::unresolved_refs(all_text(*text))) out.push_back({t.id, say("learn.lint.no_section", fmt::arg("id", ref))});
    }
    if (t.ru.hints.size() != t.en.hints.size()) out.push_back({t.id, say("learn.lint.hint_count")});
    for (int variant : variants) {
        if (t.kind == TaskKind::graph) lint_graph(t, variant, out);
        else lint_answer(t, variant, out);
        const auto shown = render_text(all_text(t.ru) + all_text(t.en), t, variant);
        for (const auto& name : placeholders(shown)) out.push_back({t.id, say("learn.lint.placeholder", fmt::arg("name", name))});
    }
    return out;
}

std::vector<LintIssue> lint_course(const Course& c, std::span<const int> variants) {
    std::vector<LintIssue> out;
    for (const auto& ch : c.chapters)
        for (const auto& t : ch.tasks) {
            auto issues = lint_task(t, variants);
            out.insert(out.end(), issues.begin(), issues.end());
        }
    return out;
}

}  // namespace signals::learn
