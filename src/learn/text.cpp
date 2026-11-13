#include <algorithm>
#include <cctype>

#include "i18n/i18n.hpp"
#include "learn/task.hpp"

namespace signals::learn {

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string{s};
}

std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= text.size()) {
        auto end = text.find('\n', i);
        if (end == std::string_view::npos) end = text.size();
        auto line = std::string{text.substr(i, end - i)};
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
        i = end + 1;
    }
    return out;
}

std::optional<std::size_t> list_marker(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    if (i == 0 || i + 1 >= line.size() || line[i] != '.' || line[i + 1] != ' ') return std::nullopt;
    return i + 2;
}

std::vector<std::string> list_items(const std::vector<std::string>& lines) {
    std::vector<std::string> out;
    for (const auto& line : lines) {
        if (auto at = list_marker(line)) out.push_back(trim(std::string_view{line}.substr(*at)));
        else if (!out.empty() && !trim(line).empty()) out.back() += " " + trim(line);
    }
    return out;
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return trim(out);
}

}  // namespace

core::Result<TaskText> parse_task_text(std::string_view markdown) {
    TaskText t;
    enum class Part { statement, goals, hints, solution } part = Part::statement;
    std::vector<std::string> statement, goals, hints, solution;
    bool titled = false;
    for (const auto& line : lines_of(markdown)) {
        if (!titled) {
            if (trim(line).empty()) continue;
            if (!line.starts_with("# ")) return core::make_error(core::Errc::invalid_argument, tr("learn.error.no_title"));
            t.title = trim(std::string_view{line}.substr(2));
            titled = true;
            continue;
        }
        if (line.starts_with("## ")) {
            if (line.find("{#goals}") != std::string::npos) {
                part = Part::goals;
                continue;
            }
            if (line.find("{#hints}") != std::string::npos) {
                part = Part::hints;
                continue;
            }
            if (line.find("{#solution}") != std::string::npos) {
                part = Part::solution;
                continue;
            }
        }
        switch (part) {
        case Part::statement: statement.push_back(line); break;
        case Part::goals: goals.push_back(line); break;
        case Part::hints: hints.push_back(line); break;
        case Part::solution: solution.push_back(line); break;
        }
    }
    if (!titled) return core::make_error(core::Errc::invalid_argument, tr("learn.error.no_title"));
    t.statement = join(statement);
    t.goals = list_items(goals);
    t.hints = list_items(hints);
    t.solution = join(solution);
    return t;
}

}  // namespace signals::learn
