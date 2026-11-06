#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace signals::tui {

int run(std::optional<std::filesystem::path> file, std::string_view theme);

}  // namespace signals::tui
