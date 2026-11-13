#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace signals::cli {

struct CliOption {
    std::vector<std::string> names = {};
    std::string value = {};
    std::string description = {};
    std::vector<std::string> choices = {};
};

struct CliEntry {
    std::vector<std::string> path = {};
    std::string description = {};
    std::vector<CliOption> options = {};
    std::vector<CliOption> positionals = {};
};

enum class ShellKind { bash, zsh, fish };

std::vector<CliEntry> cli_entries();
std::string completion_script(std::span<const CliEntry> entries, ShellKind shell);
std::string man_page(std::span<const CliEntry> entries, std::string_view version);

}  // namespace signals::cli
