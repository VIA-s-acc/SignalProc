#pragma once

#include <functional>
#include <vector>

#include "commands/cli_docs.hpp"

namespace CLI {
class App;
}

namespace signals::cli {

void with_cli_tree(const std::function<void(const CLI::App&)>& visit);
std::vector<CliEntry> cli_entries(const CLI::App& root);

}  // namespace signals::cli
