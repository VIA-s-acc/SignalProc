#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <span>
#include <string_view>
#include <vector>

#include "core/check.hpp"

namespace signals::cli {

using CheckSuite = std::vector<core::CheckResult> (*)();

std::vector<CheckSuite>& check_suites();

struct TuiRequest {
    std::optional<std::filesystem::path> file;
    std::string theme = "dark";
};

int run_cli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err,
            std::optional<TuiRequest>* tui = nullptr);

}  // namespace signals::cli
