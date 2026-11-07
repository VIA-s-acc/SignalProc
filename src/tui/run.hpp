#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

#include "i18n/i18n.hpp"

namespace signals::tui {

int run(std::optional<std::filesystem::path> file, std::string_view theme, std::optional<Language> language = std::nullopt);
void load_user_wavelets();

}  // namespace signals::tui
