#include <catch2/catch_test_macros.hpp>

#include <string>

#include "learn/lint.hpp"
#include "support/paths.hpp"

using namespace signals::learn;

namespace {

std::string joined(const std::vector<LintIssue>& issues) {
    std::string out;
    for (const auto& i : issues) out += i.task + ": " + i.message + "\n";
    return out;
}

}  // namespace

TEST_CASE("every built-in task is solvable, not pre-solved and not cheatable", "[learn][course]") {
    const auto& course = builtin_course();
    REQUIRE(course.size() > 0);
    const std::vector<int> variants{1, 2, 3};
    const auto issues = lint_course(course, variants);
    INFO(joined(issues));
    CHECK(issues.empty());
}

TEST_CASE("lint finds a pre-solved start, a passing cheat and a bad section", "[learn][course]") {
    auto files = read_course_dir(signals::test::learn_fixture("mini"));
    REQUIRE(files.has_value());
    CHECK(lint_course(*load_course(*files), std::vector<int>{1}).empty());
    auto& start = files->at("01-basics/02-double/start.sig");
    start = files->at("01-basics/02-double/solution.sig");
    files->at("01-basics/02-double/cheats/hardcoded.sig") = start;
    auto& toml = files->at("01-basics/01-sum/task.toml");
    toml.replace(toml.find("lti-convolution"), 15, "no-such-section");
    files->at("02-more/01-delayed/ru.md") += "\nСм. {#nowhere}.\n";
    auto course = load_course(*files);
    REQUIRE(course.has_value());
    const auto text = joined(lint_course(*course, std::vector<int>{1}));
    CHECK(text.find("1.2") != std::string::npos);
    CHECK(text.find("hardcoded.sig") != std::string::npos);
    CHECK(text.find("no-such-section") != std::string::npos);
    CHECK(text.find("nowhere") != std::string::npos);
}
