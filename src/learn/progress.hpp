#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "learn/checker.hpp"

namespace signals::learn {

enum class TaskState { open, solved, revealed };

struct TaskProgress {
    TaskState state = TaskState::open;
    int attempts = 0;
    int hints = 0;
    std::string solved_at = {};
    std::string answer = {};
    std::vector<std::string> results = {};
};

struct Progress {
    std::string student;
    int variant = 1;
    std::string active;
    std::map<std::string, TaskProgress, std::less<>> tasks;
};

struct LoadedProgress {
    Progress progress;
    std::optional<std::string> warning;
};

std::filesystem::path progress_path(const std::filesystem::path& config_dir);
std::filesystem::path work_path(const std::filesystem::path& config_dir, std::string_view id);
std::filesystem::path solution_path(const std::filesystem::path& config_dir, std::string_view id);
LoadedProgress load_progress(const std::filesystem::path& file);
core::Result<void> save_progress(const std::filesystem::path& file, const Progress& p);
void record_check(Progress& p, std::string_view id, const CheckResult& r, std::string_view answer, std::string_view now);
void reveal(Progress& p, std::string_view id);
std::string now_text();

}  // namespace signals::learn
