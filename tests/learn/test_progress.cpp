#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "learn/progress.hpp"
#include "support/paths.hpp"

using namespace signals::learn;
using signals::test::temp_path;

namespace {

std::filesystem::path fresh(const std::string& name) {
    const auto dir = temp_path(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("progress paths live in the config dir", "[learn][progress]") {
    const std::filesystem::path dir{"/c/signals"};
    CHECK(progress_path(dir) == dir / "learn.toml");
    CHECK(work_path(dir, "4.3") == dir / "learn" / "work" / "4.3.sig");
    CHECK(solution_path(dir, "4.3") == dir / "learn" / "4.3.sig");
}

TEST_CASE("progress round-trips through TOML", "[learn][progress]") {
    const auto file = progress_path(fresh("learn-progress"));
    Progress p;
    p.student = "Анна Иванова";
    p.variant = 17;
    p.active = "4.3";
    p.tasks["4.3"] = {TaskState::solved, 3, 1, "2026-11-13 18:20", "", {"err = 4.4e-16", ""}};
    p.tasks["1.1"] = {TaskState::revealed, 1, 2, "", "1 2 \"3\"", {}};
    REQUIRE(save_progress(file, p).has_value());
    const auto loaded = load_progress(file);
    CHECK_FALSE(loaded.warning.has_value());
    CHECK(loaded.progress.student == p.student);
    CHECK(loaded.progress.variant == 17);
    CHECK(loaded.progress.active == "4.3");
    REQUIRE(loaded.progress.tasks.size() == 2);
    const auto& t = loaded.progress.tasks.at("4.3");
    CHECK(t.state == TaskState::solved);
    CHECK(t.attempts == 3);
    CHECK(t.hints == 1);
    CHECK(t.solved_at == "2026-11-13 18:20");
    CHECK(t.results == std::vector<std::string>{"err = 4.4e-16", ""});
    CHECK(loaded.progress.tasks.at("1.1").state == TaskState::revealed);
    CHECK(loaded.progress.tasks.at("1.1").answer == "1 2 \"3\"");
}

TEST_CASE("a missing progress file starts a fresh path", "[learn][progress]") {
    const auto loaded = load_progress(fresh("learn-none") / "learn.toml");
    CHECK_FALSE(loaded.warning.has_value());
    CHECK(loaded.progress.variant == 1);
    CHECK(loaded.progress.tasks.empty());
}

TEST_CASE("a broken progress file is kept as .bak and progress restarts with a warning", "[learn][progress]") {
    const auto dir = fresh("learn-broken");
    const auto file = progress_path(dir);
    std::ofstream{file} << "variant = = 3\n[tasks\n";
    const auto loaded = load_progress(file);
    REQUIRE(loaded.warning.has_value());
    CHECK(loaded.warning->find("learn.toml.bak") != std::string::npos);
    CHECK(loaded.progress.tasks.empty());
    CHECK(std::filesystem::exists(dir / "learn.toml.bak"));
    CHECK_FALSE(std::filesystem::exists(file));
    std::ofstream{file} << "variant = \"seventeen\"\n";
    CHECK(load_progress(file).warning.has_value());
}

TEST_CASE("a check records attempts and the solved time once", "[learn][progress]") {
    Progress p;
    CheckResult fail{false, {GoalResult{false, 1.0, "err = 1"}}, 1, std::nullopt, 0.1};
    CheckResult pass{true, {GoalResult{true, 0.0, "err = 0"}}, 4, std::nullopt, 0.1};
    record_check(p, "4.3", fail, "", "t1");
    CHECK(p.tasks.at("4.3").state == TaskState::open);
    CHECK(p.tasks.at("4.3").attempts == 1);
    record_check(p, "4.3", pass, "", "t2");
    record_check(p, "4.3", pass, "", "t3");
    const auto& t = p.tasks.at("4.3");
    CHECK(t.state == TaskState::solved);
    CHECK(t.attempts == 3);
    CHECK(t.solved_at == "t2");
    CHECK(t.results == std::vector<std::string>{"err = 0"});
    record_check(p, "1.1", fail, "7", "t4");
    CHECK(p.tasks.at("1.1").answer == "7");
}

TEST_CASE("revealed stays revealed after a later solve", "[learn][progress]") {
    Progress p;
    reveal(p, "2.1");
    CHECK(p.tasks.at("2.1").state == TaskState::revealed);
    record_check(p, "2.1", CheckResult{true, {}, 1, std::nullopt, 0}, "1", "t");
    CHECK(p.tasks.at("2.1").state == TaskState::revealed);
    CHECK(p.tasks.at("2.1").solved_at == "t");
    Progress q;
    record_check(q, "2.1", CheckResult{true, {}, 1, std::nullopt, 0}, "1", "t");
    reveal(q, "2.1");
    CHECK(q.tasks.at("2.1").state == TaskState::solved);
}
