#include "learn/task.hpp"

#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <toml++/toml.hpp>

#include "i18n/i18n.hpp"

struct SignalsEmbeddedFile {
    const char* name;
    const char* text;
};
extern const SignalsEmbeddedFile kSignalsCourse[];
extern const std::size_t kSignalsCourseCount;

namespace signals::learn {

namespace {

using core::Errc;

std::unexpected<core::Error> fail_at(std::string_view file, std::size_t line, std::string message) {
    return core::make_error(Errc::invalid_argument, fmt::format(fmt::runtime(tr("learn.error.at")), fmt::arg("file", file), fmt::arg("line", line),
                                                                fmt::arg("message", message)));
}

std::unexpected<core::Error> fail_file(std::string_view file, std::string message) {
    return core::make_error(Errc::invalid_argument,
                            fmt::format(fmt::runtime(tr("learn.error.file")), fmt::arg("file", file), fmt::arg("message", message)));
}

std::string bad_value(std::string_view key, std::string_view value) {
    return fmt::format(fmt::runtime(tr("learn.error.bad_value")), fmt::arg("key", key), fmt::arg("value", value));
}

std::string missing(std::string_view file) { return fmt::format(fmt::runtime(tr("learn.error.missing")), fmt::arg("file", file)); }

std::optional<int> prefix_number(std::string_view dir) {
    std::size_t i = 0;
    while (i < dir.size() && std::isdigit(static_cast<unsigned char>(dir[i]))) ++i;
    if (i == 0 || i >= dir.size() || dir[i] != '-') return std::nullopt;
    return std::stoi(std::string{dir.substr(0, i)});
}

std::size_t line_of(const toml::node& n) { return n.source().begin.line; }

core::Result<toml::table> parse_toml(const FileMap& files, const std::string& path) {
    const auto it = files.find(path);
    if (it == files.end()) return fail_file(path, missing(path));
    try {
        return toml::parse(it->second, path);
    } catch (const toml::parse_error& e) {
        return fail_at(path, e.source().begin.line, std::string{e.description()});
    }
}

std::vector<std::string> strings(const toml::table& t, std::string_view key) {
    std::vector<std::string> out;
    if (const auto* a = t[key].as_array())
        for (const auto& v : *a)
            if (auto s = v.value<std::string>()) out.push_back(*s);
    return out;
}

std::string text_of(const toml::node_view<const toml::node>& n) {
    if (auto s = n.value<std::string>()) return *s;
    if (auto i = n.value<long long>()) return std::to_string(*i);
    if (auto d = n.value<double>()) return fmt::format("{}", *d);
    return {};
}

template <class V>
core::Result<V> pick(const std::vector<std::pair<std::string, V>>& names, const toml::table& t, std::string_view key, const std::string& path,
                     std::string_view fallback) {
    const auto node = t[key];
    const auto value = node ? node.value<std::string>().value_or("") : std::string{fallback};
    for (const auto& [name, v] : names)
        if (name == value) return v;
    return fail_at(path, node ? line_of(*node.node()) : line_of(t), bad_value(key, value));
}

core::Result<ParamSpec> parse_param(const toml::table& t, const std::string& path) {
    ParamSpec p;
    p.name = t["name"].value_or(std::string{});
    static const std::vector<std::pair<std::string, ParamSpec::Kind>> kinds{
        {"int", ParamSpec::Kind::integer}, {"ints", ParamSpec::Kind::ints}, {"real", ParamSpec::Kind::real}, {"choice", ParamSpec::Kind::choice}};
    auto kind = pick(kinds, t, "kind", path, "int");
    if (!kind) return std::unexpected{kind.error()};
    p.kind = *kind;
    p.count = static_cast<int>(t["count"].value_or(1LL));
    p.min = t["min"].value_or(0.0);
    p.max = t["max"].value_or(9.0);
    p.decimals = static_cast<int>(t["decimals"].value_or(2LL));
    p.choices = strings(t, "choices");
    if (p.name.empty()) return fail_at(path, line_of(t), bad_value("name", ""));
    return p;
}

core::Result<Measure> parse_measure(const toml::table& t, const std::string& path) {
    Measure m;
    m.name = t["name"].value_or(std::string{});
    m.kind = t["kind"].value_or(std::string{});
    m.a = t["a"].value_or(std::string{});
    m.b = t["b"].value_or(std::string{});
    m.ref = t["ref"].value_or(std::string{});
    m.block = t["block"].value_or(std::string{});
    m.field = t["field"].value_or(std::string{});
    m.param = t["param"].value_or(std::string{});
    if (t["delay"]) m.delay = text_of(t["delay"]);
    m.trim = static_cast<std::size_t>(t["trim"].value_or(0LL));
    if (const auto f = t["freq"]; f.is_string()) m.freq_text = f.value_or(std::string{});
    else m.freq = f.value_or(0.0);
    if (m.name.empty()) return fail_at(path, line_of(t), bad_value("name", ""));
    if (std::ranges::find(kMeasureKinds, m.kind) == std::end(kMeasureKinds)) {
        const auto* node = t.get("kind");
        return fail_at(path, node ? line_of(*node) : line_of(t), bad_value("kind", m.kind));
    }
    return m;
}

core::Result<Goal> parse_goal(const toml::table& t, const std::string& path) {
    Goal g;
    g.when = t["when"].value_or(std::string{});
    if (const auto* th = t["through"].as_table())
        g.through = Through{(*th)["from"].value_or(std::string{}), (*th)["to"].value_or(std::string{}), (*th)["block"].value_or(std::string{})};
    g.forbid = strings(t, "forbid");
    if (const auto* req = t["require"].as_table())
        for (const auto& [k, v] : *req) g.require.emplace_back(std::string{k.str()}, static_cast<int>(v.value_or(1LL)));
    g.probe = t["probe"].value_or(std::string{});
    const int kinds = !g.when.empty() + g.through.has_value() + !g.forbid.empty() + !g.require.empty() + !g.probe.empty();
    if (kinds != 1) return fail_at(path, line_of(t), tr("learn.error.goal_kind"));
    return g;
}

core::Result<Answer> parse_answer_spec(const toml::table& t, const std::string& path) {
    Answer a;
    static const std::vector<std::pair<std::string, AnswerType>> types{
        {"number", AnswerType::number}, {"integer", AnswerType::integer}, {"vector", AnswerType::vector}, {"choice", AnswerType::choice}};
    auto type = pick(types, t, "type", path, "number");
    if (!type) return std::unexpected{type.error()};
    a.type = *type;
    a.expected = text_of(t["expected"]);
    a.tol = t["tol"].value_or(1e-9);
    a.relative = t["rel"].value_or(false);
    a.options = strings(t, "options");
    a.verify = t["verify"].value_or(std::string{});
    if (a.expected.empty()) return fail_at(path, line_of(t), bad_value("expected", ""));
    return a;
}

core::Result<TaskText> load_text(const FileMap& files, const std::string& path, std::size_t goals) {
    const auto it = files.find(path);
    if (it == files.end()) return fail_file(path, missing(path));
    auto text = parse_task_text(it->second);
    if (!text) return fail_file(path, text.error().message);
    if (text->goals.size() != goals)
        return fail_file(path, fmt::format(fmt::runtime(tr("learn.error.goal_count")), fmt::arg("text", text->goals.size()), fmt::arg("toml", goals)));
    return text;
}

core::Result<Task> load_task(const FileMap& files, const std::string& dir, std::string id) {
    const auto path = dir + "/task.toml";
    auto table = parse_toml(files, path);
    if (!table) return std::unexpected{table.error()};
    const auto& t = *table;
    Task task;
    task.id = std::move(id);
    task.path = dir;
    static const std::vector<std::pair<std::string, TaskKind>> kinds{{"graph", TaskKind::graph}, {"answer", TaskKind::answer}};
    auto kind = pick(kinds, t, "kind", path, "graph");
    if (!kind) return std::unexpected{kind.error()};
    task.kind = *kind;
    task.open = t["open"].value_or(std::string{"pipeline"});
    task.theory = strings(t, "theory");
    task.locked = strings(t, "locked");
    if (const auto* params = t["param"].as_array())
        for (const auto& p : *params)
            if (const auto* pt = p.as_table()) {
                auto spec = parse_param(*pt, path);
                if (!spec) return std::unexpected{spec.error()};
                task.params.push_back(std::move(*spec));
            }
    if (const auto* in = t["input"].as_table()) task.input = InputSpec{(*in)["node"].value_or(std::string{}), static_cast<int>((*in)["hidden"].value_or(0LL))};
    if (const auto* ms = t["measure"].as_array())
        for (const auto& m : *ms)
            if (const auto* mt = m.as_table()) {
                auto measure = parse_measure(*mt, path);
                if (!measure) return std::unexpected{measure.error()};
                task.measures.push_back(std::move(*measure));
            }
    if (const auto* gs = t["goal"].as_array())
        for (const auto& g : *gs)
            if (const auto* gt = g.as_table()) {
                auto goal = parse_goal(*gt, path);
                if (!goal) return std::unexpected{goal.error()};
                task.goals.push_back(std::move(*goal));
            }
    if (const auto* at = t["answer"].as_table()) {
        auto answer = parse_answer_spec(*at, path);
        if (!answer) return std::unexpected{answer.error()};
        task.answer = std::move(*answer);
    }
    if (task.kind == TaskKind::answer && !task.answer) return fail_at(path, line_of(t), tr("learn.error.no_answer"));
    const auto file = [&](const std::string& name) -> const std::string* {
        const auto it = files.find(dir + "/" + name);
        return it == files.end() ? nullptr : &it->second;
    };
    for (const auto* name : {"start.sig", "solution.sig"}) {
        const auto* text = file(name);
        if (!text && task.kind == TaskKind::graph) return fail_file(dir + "/" + name, missing(dir + "/" + name));
        if (text) (std::string_view{name} == "start.sig" ? task.start_sig : task.solution_sig) = *text;
    }
    const auto cheats = dir + "/cheats/";
    for (auto it = files.lower_bound(cheats); it != files.end() && it->first.starts_with(cheats); ++it)
        task.cheats.emplace_back(it->first.substr(cheats.size()), it->second);
    auto ru = load_text(files, dir + "/ru.md", task.goals.size());
    if (!ru) return std::unexpected{ru.error()};
    auto en = load_text(files, dir + "/en.md", task.goals.size());
    if (!en) return std::unexpected{en.error()};
    task.ru = std::move(*ru);
    task.en = std::move(*en);
    return task;
}

std::string first_component(std::string_view path) { return std::string{path.substr(0, path.find('/'))}; }

}  // namespace

const TaskText& Task::text() const { return language() == Language::en ? en : ru; }

std::string Chapter::title() const { return language() == Language::en && !title_en.empty() ? title_en : title_ru; }

std::string Course::title() const { return language() == Language::en && !title_en.empty() ? title_en : title_ru; }

const Task* Course::find(std::string_view id) const {
    for (const auto& c : chapters)
        for (const auto& t : c.tasks)
            if (t.id == id) return &t;
    return nullptr;
}

std::size_t Course::size() const {
    std::size_t n = 0;
    for (const auto& c : chapters) n += c.tasks.size();
    return n;
}

core::Result<Course> load_course(const FileMap& files) {
    auto root = parse_toml(files, "course.toml");
    if (!root) return std::unexpected{root.error()};
    Course course;
    course.title_ru = (*root)["title"].value_or(std::string{});
    course.title_en = (*root)["title_en"].value_or(std::string{});
    std::map<int, std::string> chapter_dirs;
    for (const auto& [path, text] : files) {
        if (path.find('/') == std::string::npos) continue;
        const auto dir = first_component(path);
        const auto number = prefix_number(dir);
        if (!number) return fail_file(dir, fmt::format(fmt::runtime(tr("learn.error.prefix")), fmt::arg("dir", dir)));
        chapter_dirs[*number] = dir;
    }
    for (const auto& [number, dir] : chapter_dirs) {
        auto table = parse_toml(files, dir + "/chapter.toml");
        if (!table) return std::unexpected{table.error()};
        Chapter chapter;
        chapter.id = std::to_string(number);
        chapter.path = dir;
        chapter.title_ru = (*table)["title"].value_or(std::string{});
        chapter.title_en = (*table)["title_en"].value_or(std::string{});
        chapter.theory = strings(*table, "theory");
        std::map<int, std::string> task_dirs;
        const auto prefix = dir + "/";
        for (auto it = files.lower_bound(prefix); it != files.end() && it->first.starts_with(prefix); ++it) {
            const auto rest = std::string_view{it->first}.substr(prefix.size());
            if (rest.find('/') == std::string_view::npos) continue;
            const auto task_dir = first_component(rest);
            const auto task_number = prefix_number(task_dir);
            if (!task_number)
                return fail_file(prefix + task_dir, fmt::format(fmt::runtime(tr("learn.error.prefix")), fmt::arg("dir", prefix + task_dir)));
            task_dirs[*task_number] = task_dir;
        }
        for (const auto& [task_number, task_dir] : task_dirs) {
            auto task = load_task(files, prefix + task_dir, fmt::format("{}.{}", number, task_number));
            if (!task) return std::unexpected{task.error()};
            chapter.tasks.push_back(std::move(*task));
        }
        course.chapters.push_back(std::move(chapter));
    }
    return course;
}

core::Result<FileMap> read_course_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return fail_file(dir.string(), missing(dir.string()));
    FileMap files;
    for (auto it = std::filesystem::recursive_directory_iterator{dir, ec}; !ec && it != std::filesystem::recursive_directory_iterator{}; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const auto ext = it->path().extension().string();
        if (ext != ".toml" && ext != ".md" && ext != ".sig") continue;
        std::ifstream in{it->path(), std::ios::binary};
        std::ostringstream text;
        text << in.rdbuf();
        files[std::filesystem::relative(it->path(), dir).generic_string()] = text.str();
    }
    if (ec) return core::make_error(Errc::io_error, dir.string() + ": " + ec.message());
    return files;
}

const Course& builtin_course() {
    static const Course course = [] {
        FileMap files;
        for (std::size_t i = 0; i < kSignalsCourseCount; ++i) files[kSignalsCourse[i].name] = kSignalsCourse[i].text;
        auto c = load_course(files);
        if (!c) {
            std::cerr << c.error().message << '\n';
            return Course{};
        }
        return std::move(*c);
    }();
    return course;
}

std::vector<const Task*> tasks_for_section(const Course& c, std::string_view section_id) {
    std::vector<const Task*> out;
    for (const auto& chapter : c.chapters)
        for (const auto& t : chapter.tasks)
            if (std::ranges::find(t.theory, section_id) != t.theory.end()) out.push_back(&t);
    return out;
}

}  // namespace signals::learn
