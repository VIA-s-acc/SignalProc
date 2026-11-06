#include <array>
#include <optional>
#include <string>
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
    CHECK(out.str() == "signals " SIGNALS_EXPECTED_VERSION "\n");
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

TEST_CASE("--lang en switches help and errors for one run", "[cli][i18n]") {
    const auto has_cyrillic = [](const std::string& s) {
        for (unsigned char c : s)
            if (c == 0xD0 || c == 0xD1) return true;
        return false;
    };
    std::ostringstream out, err;
    std::array<std::string_view, 4> args{"signals", "frobnicate", "--lang", "en"};
    CHECK(signals::cli::run_cli(args, out, err) == 2);
    CHECK_FALSE(err.str().empty());
    CHECK_FALSE(has_cyrillic(err.str()));
    CHECK(signals::language() == signals::Language::ru);
    std::ostringstream help_out, help_err;
    std::array<std::string_view, 3> help{"signals", "--lang=en", "--help"};
    signals::cli::run_cli(help, help_out, help_err);
    CHECK(help_out.str().find("--lang") != std::string::npos);
    CHECK_FALSE(has_cyrillic(help_out.str()));
    std::ostringstream ru_err;
    std::array<std::string_view, 2> plain{"signals", "frobnicate"};
    signals::cli::run_cli(plain, out, ru_err);
    CHECK(has_cyrillic(ru_err.str()));
}

TEST_CASE("--lang reaches the TUI request", "[cli][i18n]") {
    std::ostringstream out, err;
    std::optional<signals::cli::TuiRequest> request;
    std::array<std::string_view, 4> args{"signals", "tui", "--lang", "en"};
    CHECK(signals::cli::run_cli(args, out, err, &request) == 0);
    REQUIRE(request);
    CHECK(request->language == signals::Language::en);
}

TEST_CASE("a bad --lang value is an error", "[cli][i18n]") {
    std::ostringstream out, err;
    std::array<std::string_view, 4> args{"signals", "--lang", "de", "--version"};
    CHECK(signals::cli::run_cli(args, out, err) == 2);
    CHECK(err.str().find("de") != std::string::npos);
}
