#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "learn/task.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"

using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kOld = "pipeline \"old\" v1\nnode x = wav(path=\"a$b.wav\")\n";
constexpr const char* kNew = "pipeline \"new\" v2\nnode g = gain()\n";

std::filesystem::path fresh(const std::string& name) {
    const auto dir = signals::test::temp_path(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "sub");
    std::ofstream{dir / "sub" / "old.sig", std::ios::binary} << kOld;
    std::ofstream{dir / "new.sig", std::ios::binary} << kNew;
    return dir;
}

std::string bytes(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("migrate without write changes nothing", "[cli-migrate]") {
    const auto dir = fresh("migrate-dry");
    auto [code, out, err] = signals::test::run_cli({"signals", "migrate", dir.string()});
    INFO(out << err);
    CHECK(code == 0);
    CHECK_THAT(out, ContainsSubstring("old.sig: v1 → v2"));
    CHECK_THAT(out, ContainsSubstring("x.path"));
    CHECK_THAT(out, ContainsSubstring("new.sig: v2"));
    CHECK(bytes(dir / "sub" / "old.sig") == kOld);
    CHECK_FALSE(std::filesystem::exists(dir / "sub" / "old.v1.sig"));
}

TEST_CASE("migrate write keeps a backup", "[cli-migrate]") {
    const auto dir = fresh("migrate-write");
    auto [code, out, err] = signals::test::run_cli({"signals", "migrate", "--write", dir.string()});
    INFO(out << err);
    CHECK(code == 0);
    CHECK(bytes(dir / "sub" / "old.v1.sig") == kOld);
    const auto migrated = bytes(dir / "sub" / "old.sig");
    CHECK(migrated.starts_with("pipeline \"old\" v2"));
    CHECK_THAT(migrated, ContainsSubstring("a$$b.wav"));
    CHECK(bytes(dir / "new.sig") == kNew);
}

TEST_CASE("migrate write without backup leaves no copy", "[cli-migrate]") {
    const auto dir = fresh("migrate-nobackup");
    auto [code, out, err] = signals::test::run_cli({"signals", "migrate", "--write", "--no-backup", dir.string()});
    CHECK(code == 0);
    CHECK_FALSE(std::filesystem::exists(dir / "sub" / "old.v1.sig"));
    CHECK(bytes(dir / "sub" / "old.sig").starts_with("pipeline \"old\" v2"));
}

TEST_CASE("migrate check fails on an old file and passes on current ones", "[cli-migrate]") {
    const auto dir = fresh("migrate-check");
    CHECK(std::get<0>(signals::test::run_cli({"signals", "migrate", "--check", dir.string()})) == 1);
    CHECK(std::get<0>(signals::test::run_cli({"signals", "migrate", "--check", (dir / "new.sig").string()})) == 0);
}

TEST_CASE("a broken file is reported and the rest are still migrated", "[cli-migrate]") {
    const auto dir = fresh("migrate-broken");
    std::ofstream{dir / "broken.sig", std::ios::binary} << "pipeline \"b\" v1\nnode = \n";
    auto [code, out, err] = signals::test::run_cli({"signals", "migrate", "--write", dir.string()});
    INFO(out << err);
    CHECK(code == 1);
    CHECK_THAT(out + err, ContainsSubstring("broken.sig"));
    CHECK(bytes(dir / "sub" / "old.sig").starts_with("pipeline \"old\" v2"));
}

TEST_CASE("backups are not migrated again", "[cli-migrate]") {
    const auto dir = fresh("migrate-backups");
    std::ofstream{dir / "kept.v1.sig", std::ios::binary} << kOld;
    std::filesystem::remove(dir / "sub" / "old.sig");
    auto [code, out, err] = signals::test::run_cli({"signals", "migrate", "--check", dir.string()});
    INFO(out << err);
    CHECK(code == 0);
    CHECK(out.find("kept.v1.sig") == std::string::npos);
}

TEST_CASE("an own course ignores migration backups", "[cli-migrate]") {
    const auto dir = signals::test::temp_path("migrate-course-backups");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "cheats");
    std::ofstream{dir / "cheats" / "a.sig", std::ios::binary} << kNew;
    std::ofstream{dir / "cheats" / "a.v1.sig", std::ios::binary} << kOld;
    const auto files = signals::learn::read_course_dir(dir);
    REQUIRE(files);
    CHECK(files->contains("cheats/a.sig"));
    CHECK_FALSE(files->contains("cheats/a.v1.sig"));
}
