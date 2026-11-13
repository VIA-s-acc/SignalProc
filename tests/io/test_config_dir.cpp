#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "io/config_dir.hpp"

using signals::io::config_dir;

namespace {

signals::io::EnvLookup env(std::map<std::string, std::string> vars) {
    return [vars = std::move(vars)](std::string_view k) -> std::optional<std::string> {
        auto it = vars.find(std::string{k});
        return it == vars.end() ? std::nullopt : std::optional<std::string>{it->second};
    };
}

}  // namespace

TEST_CASE("config dir follows XDG, APPDATA and HOME", "[io][config]") {
    CHECK(config_dir(env({{"XDG_CONFIG_HOME", "/x"}, {"HOME", "/h"}})) == std::filesystem::path{"/x/signals"});
    CHECK(config_dir(env({{"HOME", "/h"}})) == std::filesystem::path{"/h/.config/signals"});
    CHECK(config_dir(env({{"XDG_CONFIG_HOME", ""}, {"HOME", "/h"}})) == std::filesystem::path{"/h/.config/signals"});
    CHECK_FALSE(config_dir(env({})).has_value());
#ifdef _WIN32
    CHECK(config_dir(env({{"APPDATA", "C:/a"}, {"HOME", "/h"}})) == std::filesystem::path{"C:/a/signals"});
#endif
}
