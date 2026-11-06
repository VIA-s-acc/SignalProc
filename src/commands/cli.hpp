#pragma once

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <span>
#include <string_view>
#include <vector>

#include "core/check.hpp"

#include "i18n/i18n.hpp"

namespace signals::cli {

using CheckSuite = std::vector<core::CheckResult> (*)();

std::vector<CheckSuite>& check_suites();

struct TuiRequest {
    std::optional<std::filesystem::path> file;
    std::string theme = "dark";
    std::optional<Language> language;
};

int run_cli(std::span<const std::string_view> args, std::ostream& out, std::ostream& err,
            std::optional<TuiRequest>* tui = nullptr);

}  // namespace signals::cli
