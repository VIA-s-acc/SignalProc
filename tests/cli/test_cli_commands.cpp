#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "commands/cli.hpp"

namespace {

std::pair<int, std::string> run(std::initializer_list<std::string_view> args) {
    std::ostringstream out, err;
    std::vector<std::string_view> v(args);
    const int code = signals::cli::run_cli(v, out, err);
    return {code, out.str()};
}

std::vector<signals::core::CheckResult> failing_suite() { return {{"check.fake", "x", 1.0, 0.0}}; }

std::vector<signals::core::CheckResult> throwing_suite() { throw std::runtime_error("boom"); }

class ScopedSuite {
public:
    explicit ScopedSuite(signals::cli::CheckSuite suite) { signals::cli::check_suites().push_back(suite); }
    ~ScopedSuite() { signals::cli::check_suites().pop_back(); }
    ScopedSuite(const ScopedSuite&) = delete;
    ScopedSuite& operator=(const ScopedSuite&) = delete;
};

}  // namespace

TEST_CASE("check passes on a correct build", "[cli]") {
    auto [code, out] = run({"signals", "check", "--json"});
    CHECK(code == 0);
    auto j = nlohmann::json::parse(out);
    CHECK(j["failed"] == 0);
    CHECK(j["passed"].get<int>() > 106 * 3);
}

TEST_CASE("check exits non-zero when a check fails", "[cli]") {
    const auto [code, out] = [] {
        ScopedSuite scoped{&failing_suite};
        return run({"signals", "check", "--json"});
    }();
    CHECK(code == 1);
    CHECK(nlohmann::json::parse(out)["failed"] == 1);
}

TEST_CASE("filter info reports db2 delay and regularity", "[cli]") {
    auto [code, out] = run({"signals", "filter", "info", "db2", "--json"});
    REQUIRE(code == 0);
    auto j = nlohmann::json::parse(out);
    CHECK(j["delay"] == 3);
    CHECK(j["sobolev_f0"].get<double>() == Catch::Approx(1.0).margin(1e-6));
    CHECK(j["orthogonal"] == true);
    CHECK(j["h0"].size() == 4);
}

TEST_CASE("design lists 20 splits for p = 2 with 6 wavelets", "[cli]") {
    auto [code, out] = run({"signals", "design", "--p", "2", "--json"});
    REQUIRE(code == 0);
    auto j = nlohmann::json::parse(out);
    CHECK(j["splits"].size() == 20);
    CHECK(std::ranges::count_if(j["splits"], [](const auto& s) { return s["wavelet"].template get<bool>(); }) == 6);
}

TEST_CASE("unknown wavelet in filter info exits with 2", "[cli]") {
    CHECK(run({"signals", "filter", "info", "db39"}).first == 2);
}

TEST_CASE("human readable filter info is localized", "[cli]") {
    auto [code, out] = run({"signals", "filter", "info", "haar"});
    CHECK(code == 0);
    CHECK(out.find("задержка") != std::string::npos);
}

TEST_CASE("human readable design output is localized", "[cli]") {
    auto [code, out] = run({"signals", "design", "--p", "2"});
    CHECK(code == 0);
    CHECK(out.find("линейная фаза") != std::string::npos);
    CHECK(out.find("linear_phase") == std::string::npos);
}

TEST_CASE("help prints usage and exits with 0", "[cli]") {
    for (auto args : {std::initializer_list<std::string_view>{"signals", "--help"},
                      std::initializer_list<std::string_view>{"signals", "check", "--help"}}) {
        auto [code, out] = run(args);
        CHECK(code == 0);
        CHECK((out.find("--json") != std::string::npos || out.find("check") != std::string::npos));
    }
}

TEST_CASE("unknown wavelet error is localized", "[cli]") {
    std::ostringstream out, err;
    const std::vector<std::string_view> args{"signals", "filter", "info", "db39"};
    CHECK(signals::cli::run_cli(args, out, err) == 2);
    CHECK(err.str().find("неизвестный вейвлет") != std::string::npos);
}

TEST_CASE("design reports skipped splits", "[cli]") {
    auto [code, out] = run({"signals", "design", "--p", "2", "--json"});
    REQUIRE(code == 0);
    CHECK(nlohmann::json::parse(out)["skipped"] == 0);
}

TEST_CASE("an unexpected exception exits with 2 and a localized message", "[cli]") {
    ScopedSuite scoped{&throwing_suite};
    std::ostringstream out, err;
    const std::vector<std::string_view> args{"signals", "check"};
    CHECK(signals::cli::run_cli(args, out, err) == 2);
    CHECK(err.str().find("непредвиденная ошибка (boom)") != std::string::npos);
}

TEST_CASE("m7b: denoise --help names every threshold rule it accepts", "[cli]") {
    for (const auto language : {signals::Language::ru, signals::Language::en}) {
        const signals::LanguageScope scope{language};
        const auto description = signals::tr("cli.denoise.rule");
        for (const char* rule : {"bayes", "visu", "sure", "minimax"}) {
            INFO(description << " / " << rule);
            CHECK(description.find(rule) != std::string::npos);
        }
    }
}
