#include <catch2/catch_test_macros.hpp>

#include <array>
#include <map>
#include <string>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "commands/migrate_files.hpp"
#include "learn/lint.hpp"
#include "learn/migrate_course.hpp"
#include "learn/task.hpp"
#include "support/paths.hpp"
#include "pipeline/renames.hpp"

using signals::pipeline::Rename;

namespace {

const std::array<Rename, 3> kFake{
    Rename{Rename::Kind::block, "dwt", "", "dwt", "wavedec", "test"},
    Rename{Rename::Kind::param, "gain", "", "db", "level_db", "test"},
    Rename{Rename::Kind::field, "check", "", "snr_db", "ratio_db", "test"},
};

}  // namespace

TEST_CASE("task toml keeps comments and renames values and require keys", "[migrate-course]") {
    std::string text = "kind = \"graph\"  # a graph task\n"
                       "\n"
                       "[[measure]]\n"
                       "name = \"n\"\n"
                       "kind = \"count\"\n"
                       "block = \"dwt\"  # how many\n"
                       "\n"
                       "[[measure]]\n"
                       "name = \"s\"\n"
                       "kind = \"field\"\n"
                       "a = \"chk\"\n"
                       "field = \"snr_db\"\n"
                       "\n"
                       "[[measure]]\n"
                       "name = \"p\"\n"
                       "kind = \"param\"\n"
                       "param = \"g.db\"\n"
                       "\n"
                       "[[goal]]\n"
                       "when = \"s >= 30 && n == 1\"\n"
                       "\n"
                       "[[goal]]\n"
                       "require = { dwt = 1 } # c\n"
                       "\n"
                       "[[goal]]\n"
                       "forbid = [\"dwt\", \"idwt\"]\n"
                       "\n"
                       "[[goal]]\n"
                       "through = { from = \"x\", to = \"y\", block = \"dwt\" }\n";
    const std::map<std::string, std::string, std::less<>> nodes{{"chk", "check"}, {"g", "gain"}};
    const auto notes = signals::learn::migrate_task_toml(text, kFake, nodes);
    REQUIRE(notes);
    CHECK(notes->size() == 6);
    CHECK(text == "kind = \"graph\"  # a graph task\n"
                  "\n"
                  "[[measure]]\n"
                  "name = \"n\"\n"
                  "kind = \"count\"\n"
                  "block = \"wavedec\"  # how many\n"
                  "\n"
                  "[[measure]]\n"
                  "name = \"s\"\n"
                  "kind = \"field\"\n"
                  "a = \"chk\"\n"
                  "field = \"ratio_db\"\n"
                  "\n"
                  "[[measure]]\n"
                  "name = \"p\"\n"
                  "kind = \"param\"\n"
                  "param = \"g.level_db\"\n"
                  "\n"
                  "[[goal]]\n"
                  "when = \"s >= 30 && n == 1\"\n"
                  "\n"
                  "[[goal]]\n"
                  "require = { wavedec = 1 } # c\n"
                  "\n"
                  "[[goal]]\n"
                  "forbid = [\"wavedec\", \"idwt\"]\n"
                  "\n"
                  "[[goal]]\n"
                  "through = { from = \"x\", to = \"y\", block = \"wavedec\" }\n");
}

TEST_CASE("a task toml without old names is left as it is", "[migrate-course]") {
    std::string text = "kind = \"answer\"\n";
    const auto notes = signals::learn::migrate_task_toml(text, kFake, {});
    REQUIRE(notes);
    CHECK(notes->empty());
    CHECK(text == "kind = \"answer\"\n");
}

TEST_CASE("migrate rewrites task toml in a course folder", "[migrate-course]") {
    const auto dir = signals::test::temp_path("migrate-course-folder") / "01-a" / "01-b";
    std::filesystem::remove_all(dir.parent_path().parent_path());
    std::filesystem::create_directories(dir);
    std::ofstream{dir / "task.toml", std::ios::binary} << "kind = \"graph\"\n\n[[goal]]\nforbid = [\"dwt\"]  # no dwt\n";
    std::ofstream{dir / "solution.sig", std::ios::binary} << "pipeline \"s\" v2\nnode d = dwt()\n";
    const std::vector<std::filesystem::path> paths{dir.parent_path().parent_path()};
    const auto reports = signals::cli::migrate_paths(paths, true, true, kFake);
    const auto task = std::ranges::find_if(reports, [](const auto& r) { return r.file.filename() == "task.toml"; });
    REQUIRE(task != reports.end());
    CHECK(task->written);
    std::ifstream in{dir / "task.toml", std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text == "kind = \"graph\"\n\n[[goal]]\nforbid = [\"wavedec\"]  # no dwt\n");
    CHECK(std::filesystem::exists(dir / "task.v1.toml"));
}

TEST_CASE("lint warns about a v1 graph but the course still passes", "[migrate-course]") {
    const auto source = signals::test::learn_fixture("mini");
    const auto dir = signals::test::temp_path("migrate-course-lint");
    std::filesystem::remove_all(dir);
    std::filesystem::copy(source, dir, std::filesystem::copy_options::recursive);
    const auto solution = dir / "01-basics" / "02-double" / "solution.sig";
    std::ifstream in{solution, std::ios::binary};
    std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    in.close();
    const auto header = text.find("\" v2");
    REQUIRE(header != std::string::npos);
    text.replace(header + 3, 1, "1");
    std::ofstream{solution, std::ios::binary} << text;
    const auto course = signals::learn::load_course(*signals::learn::read_course_dir(dir));
    REQUIRE(course);
    const std::vector<int> variants{1};
    const auto issues = signals::learn::lint_course(*course, variants);
    REQUIRE_FALSE(issues.empty());
    for (const auto& i : issues) {
        INFO(i.task << ": " << i.message);
        CHECK(i.warning);
    }
}
