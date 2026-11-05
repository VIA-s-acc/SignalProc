#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "commands/cli.hpp"
#include "support/cli_run.hpp"

TEST_CASE("tui subcommand fills a launch request", "[cli][tui]") {
    std::optional<signals::cli::TuiRequest> req;
    std::ostringstream out, err;
    const std::vector<std::string_view> args{"signals", "tui", "x.wav", "--theme", "light"};
    CHECK(signals::cli::run_cli(args, out, err, &req) == 0);
    REQUIRE(req);
    CHECK(req->file == std::filesystem::path{"x.wav"});
    CHECK(req->theme == "light");
}

TEST_CASE("tui without a file opens the empty interface", "[cli][tui]") {
    std::optional<signals::cli::TuiRequest> req;
    std::ostringstream out, err;
    const std::vector<std::string_view> args{"signals", "tui"};
    CHECK(signals::cli::run_cli(args, out, err, &req) == 0);
    REQUIRE(req);
    CHECK_FALSE(req->file);
    CHECK(req->theme == "dark");
}

TEST_CASE("tui without a launcher is a clear error", "[cli][tui]") {
    auto [code, out, err] = signals::test::run_cli({"signals", "tui"});
    CHECK(code == 2);
    CHECK(err.find("Ошибка") != std::string::npos);
}

TEST_CASE("signals with no arguments outside a terminal prints help", "[cli]") {
    auto [code, out, err] = signals::test::run_cli({"signals"});
    CHECK(code == 0);
    CHECK(out.find("check") != std::string::npos);
}
