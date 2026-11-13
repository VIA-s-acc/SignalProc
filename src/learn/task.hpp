#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "learn/variant.hpp"

namespace signals::learn {

enum class TaskKind { graph, answer };
enum class AnswerType { number, integer, vector, choice };

struct Measure {
    std::string name, kind, a, b, ref, block, field, param;
    std::string delay = "0";
    std::size_t trim = 0;
    double freq = 0;
};

struct Through {
    std::string from, to, block;
};

struct Goal {
    std::string when = {};
    std::optional<Through> through = {};
    std::vector<std::string> forbid = {};
    std::vector<std::pair<std::string, int>> require = {};
    std::string probe = {};
};

struct Answer {
    AnswerType type = AnswerType::number;
    std::string expected;
    double tol = 1e-9;
    bool relative = false;
    std::vector<std::string> options = {};
    std::string verify = {};
};

struct InputSpec {
    std::string node;
    int hidden = 0;
};

struct TaskText {
    std::string title, statement;
    std::vector<std::string> goals, hints;
    std::string solution;
};

struct Task {
    std::string id, path;
    TaskKind kind = TaskKind::graph;
    std::string open = "pipeline";
    std::vector<std::string> theory;
    std::vector<ParamSpec> params;
    std::optional<InputSpec> input;
    std::vector<std::string> locked;
    std::vector<Measure> measures;
    std::vector<Goal> goals;
    std::optional<Answer> answer;
    std::string start_sig, solution_sig;
    std::vector<std::pair<std::string, std::string>> cheats;
    TaskText ru, en;
    const TaskText& text() const;
};

struct Chapter {
    std::string id, path, title_ru, title_en;
    std::vector<std::string> theory;
    std::vector<Task> tasks;
    std::string title() const;
};

struct Course {
    std::string title_ru, title_en;
    std::vector<Chapter> chapters;
    const Task* find(std::string_view id) const;
    std::size_t size() const;
    std::string title() const;
};

using FileMap = std::map<std::string, std::string, std::less<>>;

core::Result<Course> load_course(const FileMap& files);
core::Result<FileMap> read_course_dir(const std::filesystem::path& dir);
const Course& builtin_course();
core::Result<TaskText> parse_task_text(std::string_view markdown);
std::vector<const Task*> tasks_for_section(const Course& c, std::string_view section_id);

inline constexpr std::string_view kMeasureKinds[] = {"max_error", "snr", "delay", "tone_db", "gain_db", "max_abs", "energy",
                                                     "rms_db", "length", "kept", "field", "samples", "count", "param"};

}  // namespace signals::learn
