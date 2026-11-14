#pragma once

#include <filesystem>
#include <functional>
#include <vector>

namespace signals::cli {

std::vector<std::filesystem::path> watched_files(const std::filesystem::path& sig);
void stop_watching();
bool watching_stopped();
void restart_watching();

int watch_loop(const std::function<int()>& once, const std::function<std::vector<std::filesystem::path>()>& files, const std::function<bool()>& keep_going,
               const std::function<void()>& pause);

}  // namespace signals::cli
