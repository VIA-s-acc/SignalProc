#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "tui/term/caps.hpp"

namespace signals::tui {

std::string base64(std::string_view bytes);
std::string osc52(std::string_view text);
std::vector<std::string> editor_command(const std::filesystem::path& file, const EnvLookup& env, std::string_view configured = {});
bool launch(const std::vector<std::string>& argv);

}  // namespace signals::tui
