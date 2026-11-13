#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace signals::io {

using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

EnvLookup process_env();

std::optional<std::filesystem::path> config_dir(const EnvLookup& env);

}  // namespace signals::io
