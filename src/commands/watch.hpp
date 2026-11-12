#pragma once

#include <filesystem>
#include <functional>
#include <vector>

namespace signals::cli {

std::vector<std::filesystem::path> watched_files(const std::filesystem::path& sig);
int watch_loop(const std::function<int()>& once, const std::function<std::vector<std::filesystem::path>()>& files, const std::function<bool()>& keep_going,
               const std::function<void()>& pause);

}  // namespace signals::cli
