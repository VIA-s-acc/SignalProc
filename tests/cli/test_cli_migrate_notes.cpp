#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include "support/cli_run.hpp"
#include "support/paths.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kOld = "pipeline \"m\" v1\nnode x = samples(values=\"1 2 3 4\", rate=8)\nnode g = gain(db=-6)\nx -> g\nprobe g as \"g\"\n";

std::filesystem::path write_file(const std::string& name, const std::string& text) {
    const auto dir = signals::test::temp_path("cli-migrate-notes");
    std::filesystem::create_directories(dir);
    const auto file = dir / name;
    std::ofstream{file, std::ios::binary} << text;
    return file;
}

std::string read_file(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("run on a v1 file says it migrated in memory", "[cli-migrate]") {
    const auto file = write_file("old.sig", kOld);
    auto [code, out, err] = signals::test::run_cli({"signals", "run", file.string()});
    INFO(err);
    CHECK(code == 0);
    CHECK_THAT(err, ContainsSubstring("migrate --write"));
    CHECK(read_file(file) == kOld);
}

TEST_CASE("run on a v2 file says nothing about migration", "[cli-migrate]") {
    std::string current = kOld;
    current.replace(current.find(" v1"), 3, " v2");
    const auto file = write_file("new.sig", current);
    auto [code, out, err] = signals::test::run_cli({"signals", "run", file.string()});
    CHECK(code == 0);
    CHECK(err.find("migrate --write") == std::string::npos);
}

TEST_CASE("run json carries migration fields", "[cli-migrate]") {
    const auto file = write_file("old-json.sig", kOld);
    auto [code, out, err] = signals::test::run_cli({"signals", "run", file.string(), "--json"});
    REQUIRE(code == 0);
    const auto j = nlohmann::json::parse(out);
    CHECK(j.at("migrated_from") == 1);
    CHECK(j.at("migration_notes").is_array());
}
