#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace signals::cli {

struct FileReport {
    std::filesystem::path file;
    int from = 0;
    std::vector<std::string> notes;
    std::optional<std::string> error;
    bool written = false;
};

bool is_migration_backup(const std::filesystem::path& file);
std::vector<FileReport> migrate_paths(std::span<const std::filesystem::path> paths, bool write, bool backup);

}  // namespace signals::cli
