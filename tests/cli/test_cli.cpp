#include <array>
#include <sstream>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "commands/cli.hpp"
#include "i18n/i18n.hpp"

TEST_CASE("version prints localized line", "[cli]") {
    std::ostringstream out, err;
    std::array<std::string_view, 2> args{"signals", "--version"};
    CHECK(signals::cli::run_cli(args, out, err) == 0);
    CHECK(out.str() == "signals 0.2.0-dev\n");
}

TEST_CASE("check passes and emits json", "[cli]") {
    std::ostringstream out, err;
    std::array<std::string_view, 3> args{"signals", "check", "--json"};
    CHECK(signals::cli::run_cli(args, out, err) == 0);
    auto j = nlohmann::json::parse(out.str());
    CHECK(j["failed"] == 0);
}

TEST_CASE("unknown subcommand exits with 2", "[cli]") {
    std::ostringstream out, err;
    std::array<std::string_view, 2> args{"signals", "frobnicate"};
    CHECK(signals::cli::run_cli(args, out, err) == 2);
    CHECK_FALSE(err.str().empty());
}

TEST_CASE("tr returns key for unknown entries", "[cli][i18n]") {
    CHECK(signals::tr("no.such.key") == "no.such.key");
    CHECK(signals::tr("cli.version") == "signals {version}");
}
