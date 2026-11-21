#pragma once

#include <map>
#include <span>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "pipeline/renames.hpp"

namespace signals::learn {

struct Task;

std::map<std::string, std::string, std::less<>> node_types_of(std::string_view sig);
std::vector<std::string> rename_task(Task& task, std::span<const pipeline::Rename> renames);
core::Result<std::vector<std::string>> migrate_task_toml(std::string& text, std::span<const pipeline::Rename> renames,
                                                         const std::map<std::string, std::string, std::less<>>& node_types);

}  // namespace signals::learn
