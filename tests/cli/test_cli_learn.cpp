#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "commands/cli.hpp"
#include "i18n/i18n.hpp"
#include "learn/checker.hpp"
#include "learn/progress.hpp"
#include "learn/task.hpp"
#include "support/paths.hpp"

using namespace signals::learn;

namespace {

struct Ran {
    int code;
    std::string out, err;
};

struct Env {
    std::filesystem::path dir;
    std::string course = signals::test::learn_fixture("mini").string();
    std::string progress;

    explicit Env(const std::string& name) : dir{signals::test::temp_path(name)} {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        progress = (dir / "learn.toml").string();
    }

    Ran run(std::vector<std::string> args) const {
        args.insert(args.begin(), {"signals", "learn"});
        for (const auto& extra : {std::string{"--course"}, course, std::string{"--progress"}, progress}) args.push_back(extra);
        std::vector<std::string_view> v(args.begin(), args.end());
        std::ostringstream out, err;
        signals::LanguageScope ru{signals::Language::ru};
        const int code = signals::cli::run_cli(v, out, err);
        return {code, out.str(), err.str()};
    }

    Progress progress_file() const { return load_progress(progress).progress; }
};

const Task& mini_task(std::string_view id) {
    static const Course c = *load_course(*read_course_dir(signals::test::learn_fixture("mini")));
    return *c.find(id);
}

}  // namespace

TEST_CASE("learn lists chapters with progress", "[cli][learn]") {
    Env env{"cli-learn-list"};
    auto r = env.run({});
    CHECK(r.code == 0);
    CHECK(r.out.find("Основы") != std::string::npos);
    CHECK(r.out.find("1.2") != std::string::npos);
    CHECK(r.out.find("Удвоение") != std::string::npos);
    CHECK(r.out.find("0/4") != std::string::npos);
    r = env.run({"list", "--json"});
    REQUIRE(r.code == 0);
    const auto j = nlohmann::json::parse(r.out);
    CHECK(j["variant"] == 1);
    CHECK(j["chapters"].size() == 2);
    CHECK(j["chapters"][0]["tasks"][1]["id"] == "1.2");
    CHECK(j["chapters"][0]["tasks"][1]["kind"] == "graph");
    CHECK(j["chapters"][0]["tasks"][1]["state"] == "open");
}

TEST_CASE("learn show renders the statement with the variant", "[cli][learn]") {
    Env env{"cli-learn-show"};
    const auto r = env.run({"show", "1.1"});
    CHECK(r.code == 0);
    const auto a = draw_params(mini_task("1.1").params, "1.1", 1).at("a");
    CHECK(r.out.find("Чему равно " + a + " + 1?") != std::string::npos);
    CHECK(r.out.find("Прибавь единицу") == std::string::npos);
    CHECK(env.run({"show", "9.9"}).code != 0);
}

TEST_CASE("learn start writes the start graph", "[cli][learn]") {
    Env env{"cli-learn-start"};
    const auto file = (env.dir / "1.2.sig").string();
    auto r = env.run({"start", "1.2", "-o", file});
    CHECK(r.code == 0);
    std::ifstream in{file};
    std::stringstream text;
    text << in.rdbuf();
    CHECK(text.str() == start_graph(mini_task("1.2"), 1));
    CHECK(env.progress_file().active == "1.2");
    std::ofstream{file} << "edited";
    env.run({"start", "1.2", "-o", file});
    std::ifstream again{file};
    std::stringstream kept;
    kept << again.rdbuf();
    CHECK(kept.str() == "edited");
}

TEST_CASE("learn check passes the solution and fails the start with exit 3", "[cli][learn]") {
    Env env{"cli-learn-check"};
    const auto start = env.dir / "start.sig";
    std::ofstream{start} << start_graph(mini_task("1.2"), 1);
    auto bad = env.run({"check", "1.2", start.string()});
    CHECK(bad.code == 3);
    CHECK(bad.out.find("✗") != std::string::npos);
    const auto good = env.dir / "good.sig";
    std::ofstream{good} << solution_graph(mini_task("1.2"), 1);
    auto ok = env.run({"check", "1.2", good.string()});
    CHECK(ok.code == 0);
    CHECK(ok.out.find("✓") != std::string::npos);
    const auto p = env.progress_file();
    CHECK(p.tasks.at("1.2").state == TaskState::solved);
    CHECK(p.tasks.at("1.2").attempts == 2);
    CHECK(std::filesystem::exists(env.dir / "learn" / "1.2.sig"));
    std::ofstream{env.dir / "broken.sig"} << "pipeline \"t\" v1\nnode = 1\n";
    CHECK(env.run({"check", "1.2", (env.dir / "broken.sig").string()}).code == 1);
    CHECK(env.run({"check", "1.2", (env.dir / "missing.sig").string()}).code == 1);
}

TEST_CASE("learn check --answer", "[cli][learn]") {
    Env env{"cli-learn-answer"};
    const auto a = std::stoi(draw_params(mini_task("1.1").params, "1.1", 1).at("a"));
    CHECK(env.run({"check", "1.1", "--answer", std::to_string(a + 5)}).code == 3);
    auto r = env.run({"check", "1.1", "--answer", std::to_string(a + 1), "--json"});
    CHECK(r.code == 0);
    const auto j = nlohmann::json::parse(r.out);
    CHECK(j["passed"] == true);
    CHECK(j["id"] == "1.1");
    CHECK(env.progress_file().tasks.at("1.1").answer == std::to_string(a + 1));
    CHECK(env.run({"check", "1.1"}).code == 2);
}

TEST_CASE("hint opens hints one by one", "[cli][learn]") {
    Env env{"cli-learn-hint"};
    auto first = env.run({"hint", "1.1"});
    CHECK(first.code == 0);
    CHECK(first.out.find("Прибавь единицу") != std::string::npos);
    CHECK(first.out.find("Получится") == std::string::npos);
    auto second = env.run({"hint", "1.1"});
    CHECK(second.out.find("Получится") != std::string::npos);
    auto third = env.run({"hint", "1.1"});
    CHECK(third.code == 0);
    CHECK(env.progress_file().tasks.at("1.1").hints == 2);
    CHECK(env.run({"show", "1.1"}).out.find("Получится") != std::string::npos);
}

TEST_CASE("solution needs --yes before solving", "[cli][learn]") {
    Env env{"cli-learn-solution"};
    auto r = env.run({"solution", "1.1"});
    CHECK(r.code == 2);
    CHECK(r.out.find("{{") == std::string::npos);
    CHECK_FALSE(env.progress_file().tasks.contains("1.1"));
    r = env.run({"solution", "1.1", "--yes"});
    CHECK(r.code == 0);
    const auto a = std::stoi(draw_params(mini_task("1.1").params, "1.1", 1).at("a"));
    CHECK(r.out.find(std::to_string(a) + " + 1 = " + std::to_string(a + 1)) != std::string::npos);
    CHECK(env.progress_file().tasks.at("1.1").state == TaskState::revealed);
}

TEST_CASE("variant changes numbers", "[cli][learn]") {
    Env env{"cli-learn-variant"};
    CHECK(env.run({"variant"}).out.find("1") != std::string::npos);
    CHECK(env.run({"variant", "17", "--student", "Анна"}).code == 0);
    CHECK(env.progress_file().variant == 17);
    CHECK(env.progress_file().student == "Анна");
    const auto a = draw_params(mini_task("1.1").params, "1.1", 17).at("a");
    CHECK(env.run({"show", "1.1"}).out.find("Чему равно " + a + " + 1?") != std::string::npos);
    const auto once = draw_params(mini_task("1.1").params, "1.1", 5).at("a");
    CHECK(env.run({"show", "1.1", "--variant", "5"}).out.find("Чему равно " + once + " + 1?") != std::string::npos);
    CHECK(env.progress_file().variant == 17);
    CHECK(env.run({"variant", "1000"}).code == 2);
}

TEST_CASE("report writes markdown", "[cli][learn]") {
    Env env{"cli-learn-report"};
    const auto a = std::stoi(draw_params(mini_task("1.1").params, "1.1", 1).at("a"));
    env.run({"check", "1.1", "--answer", std::to_string(a + 1)});
    const auto out = env.dir / "report.md";
    CHECK(env.run({"report", "-o", out.string()}).code == 0);
    std::ifstream in{out};
    std::stringstream text;
    text << in.rdbuf();
    CHECK(text.str().find("# Учебный путь: отчёт") != std::string::npos);
    CHECK(text.str().find("## 1.1 Сумма") != std::string::npos);
    CHECK(env.run({"report"}).out.find("1 из 4") != std::string::npos);
}

TEST_CASE("lint on a broken course exits 1 and names the task", "[cli][learn]") {
    Env env{"cli-learn-lint"};
    CHECK(env.run({"lint"}).code == 0);
    const auto broken = env.dir / "course";
    std::filesystem::copy(env.course, broken, std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(broken / "01-basics/02-double/solution.sig", broken / "01-basics/02-double/start.sig",
                               std::filesystem::copy_options::overwrite_existing);
    const auto path = broken.string();
    std::vector<std::string_view> v{"signals", "learn", "lint", "--course", path, "--variants", "1"};
    std::ostringstream out, err;
    CHECK(signals::cli::run_cli(v, out, err) == 1);
    CHECK((out.str() + err.str()).find("1.2") != std::string::npos);
    std::vector<std::string_view> missing{"signals", "learn", "lint", "--course", "/nonexistent/course"};
    std::ostringstream o2, e2;
    CHECK(signals::cli::run_cli(missing, o2, e2) == 1);
}

TEST_CASE("choice tasks list their options", "[cli][learn]") {
    const auto dir = signals::test::temp_path("cli-learn-options");
    std::filesystem::create_directories(dir);
    const auto progress = (dir / "p.toml").string();
    std::vector<std::string_view> args{"signals", "learn", "show", "2.4", "--progress", progress};
    std::ostringstream out, err;
    signals::LanguageScope ru{signals::Language::ru};
    REQUIRE(signals::cli::run_cli(args, out, err) == 0);
    const auto* t = builtin_course().find("2.4");
    REQUIRE(t != nullptr);
    const auto a = variant_answer(*t, 1);
    CHECK(out.str().find("1. " + a.options[0]) != std::string::npos);
    CHECK(out.str().find(std::to_string(a.options.size()) + ". " + a.options.back()) != std::string::npos);
    std::vector<std::string_view> json{"signals", "learn", "show", "2.4", "--json", "--progress", progress};
    std::ostringstream jout, jerr;
    REQUIRE(signals::cli::run_cli(json, jout, jerr) == 0);
    CHECK(nlohmann::json::parse(jout.str())["options"].size() == a.options.size());
}
