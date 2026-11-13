#include "learn/progress.hpp"

#include <chrono>
#include <ctime>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <sstream>
#include <toml++/toml.hpp>

#include "i18n/i18n.hpp"
#include "io/temp_file.hpp"

namespace signals::learn {

namespace {

constexpr std::string_view kStates[] = {"open", "solved", "revealed"};

struct Broken {
    std::string why;
};

Progress parse_progress(const toml::table& t) {
    Progress p;
    if (const auto* s = t.get("student")) {
        if (!s->is_string()) throw Broken{"student"};
        p.student = s->value_or(std::string{});
    }
    if (const auto* v = t.get("variant")) {
        if (!v->is_integer()) throw Broken{"variant"};
        p.variant = static_cast<int>(v->value_or(1LL));
    }
    p.active = t["active"].value_or(std::string{});
    if (const auto* tasks = t.get("tasks")) {
        if (!tasks->is_table()) throw Broken{"tasks"};
        for (const auto& [id, node] : *tasks->as_table()) {
            const auto* row = node.as_table();
            if (!row) throw Broken{std::string{id.str()}};
            TaskProgress tp;
            const auto state = (*row)["state"].value_or(std::string{"open"});
            const auto it = std::ranges::find(kStates, state);
            if (it == std::end(kStates)) throw Broken{std::string{id.str()} + ".state"};
            tp.state = static_cast<TaskState>(it - std::begin(kStates));
            tp.attempts = static_cast<int>((*row)["attempts"].value_or(0LL));
            tp.hints = static_cast<int>((*row)["hints"].value_or(0LL));
            tp.solved_at = (*row)["solved_at"].value_or(std::string{});
            tp.answer = (*row)["answer"].value_or(std::string{});
            if (const auto* results = (*row)["results"].as_array())
                for (const auto& r : *results) tp.results.push_back(r.value_or(std::string{}));
            p.tasks[std::string{id.str()}] = std::move(tp);
        }
    }
    if (p.variant < 1 || p.variant > 999) throw Broken{"variant"};
    return p;
}

}  // namespace

std::filesystem::path progress_path(const std::filesystem::path& config_dir) { return config_dir / "learn.toml"; }

std::filesystem::path work_path(const std::filesystem::path& config_dir, std::string_view id) {
    return config_dir / "learn" / "work" / (std::string{id} + ".sig");
}

std::filesystem::path solution_path(const std::filesystem::path& config_dir, std::string_view id) {
    return config_dir / "learn" / (std::string{id} + ".sig");
}

LoadedProgress load_progress(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return {};
    std::string why;
    try {
        return {parse_progress(toml::parse_file(file.string())), std::nullopt};
    } catch (const toml::parse_error& e) {
        why = std::string{e.description()};
    } catch (const Broken& b) {
        why = b.why;
    }
    auto bak = file;
    bak += ".bak";
    std::filesystem::remove(bak, ec);
    std::filesystem::rename(file, bak, ec);
    return {{}, fmt::format(fmt::runtime(tr("learn.progress.broken")), fmt::arg("file", file.string()), fmt::arg("why", why),
                            fmt::arg("bak", bak.string()))};
}

core::Result<void> save_progress(const std::filesystem::path& file, const Progress& p) {
    toml::table root;
    if (!p.student.empty()) root.insert("student", p.student);
    root.insert("variant", static_cast<long long>(p.variant));
    if (!p.active.empty()) root.insert("active", p.active);
    toml::table tasks;
    for (const auto& [id, t] : p.tasks) {
        toml::table row;
        row.insert("state", std::string{kStates[static_cast<int>(t.state)]});
        row.insert("attempts", static_cast<long long>(t.attempts));
        row.insert("hints", static_cast<long long>(t.hints));
        if (!t.solved_at.empty()) row.insert("solved_at", t.solved_at);
        if (!t.answer.empty()) row.insert("answer", t.answer);
        if (!t.results.empty()) {
            toml::array results;
            for (const auto& r : t.results) results.push_back(r);
            row.insert("results", std::move(results));
        }
        tasks.insert(id, std::move(row));
    }
    root.insert("tasks", std::move(tasks));
    std::ostringstream out;
    out << root << '\n';
    std::error_code ec;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), ec);
    return io::write_file_atomic(file, out.str());
}

void record_check(Progress& p, std::string_view id, const CheckResult& r, std::string_view answer, std::string_view now) {
    auto& t = p.tasks[std::string{id}];
    ++t.attempts;
    if (!answer.empty()) t.answer = std::string{answer};
    if (!r.passed) return;
    if (t.solved_at.empty()) t.solved_at = std::string{now};
    if (t.state == TaskState::open) t.state = TaskState::solved;
    t.results.clear();
    for (const auto& g : r.goals) t.results.push_back(g.detail);
}

void reveal(Progress& p, std::string_view id) {
    auto& t = p.tasks[std::string{id}];
    if (t.state == TaskState::open) t.state = TaskState::revealed;
}

std::string now_text() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return fmt::format("{:%Y-%m-%d %H:%M}", tm);
}

}  // namespace signals::learn
