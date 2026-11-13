#include "io/config_dir.hpp"

#include <cstdlib>

namespace signals::io {

namespace {

std::optional<std::string> non_empty(const EnvLookup& env, std::string_view key) {
    auto v = env(key);
    if (v && v->empty()) return std::nullopt;
    return v;
}

}  // namespace

EnvLookup process_env() {
    return [](std::string_view key) -> std::optional<std::string> {
        const char* v = std::getenv(std::string{key}.c_str());
        return v ? std::optional<std::string>{v} : std::nullopt;
    };
}

std::optional<std::filesystem::path> config_dir(const EnvLookup& env) {
    if (auto xdg = non_empty(env, "XDG_CONFIG_HOME")) return std::filesystem::path{*xdg} / "signals";
#ifdef _WIN32
    if (auto appdata = non_empty(env, "APPDATA")) return std::filesystem::path{*appdata} / "signals";
#endif
    if (auto home = non_empty(env, "HOME")) return std::filesystem::path{*home} / ".config" / "signals";
    return std::nullopt;
}

}  // namespace signals::io
