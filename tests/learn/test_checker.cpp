#include <catch2/catch_test_macros.hpp>

#include <string>

#include "i18n/i18n.hpp"
#include "learn/checker.hpp"
#include "learn/variant.hpp"
#include "support/paths.hpp"

using namespace signals::learn;
using signals::test::learn_fixture;

namespace {

const Course& mini() {
    static const Course c = [] {
        auto files = read_course_dir(learn_fixture("mini"));
        REQUIRE(files.has_value());
        auto course = load_course(*files);
        REQUIRE(course.has_value());
        return *course;
    }();
    return c;
}

const Task& task(std::string_view id) {
    const auto* t = mini().find(id);
    REQUIRE(t != nullptr);
    return *t;
}

CheckResult check_graph(const Task& t, const std::string& sig, int variant = 1) {
    signals::core::CancelToken cancel;
    auto r = check_task(t, variant, {.sig = sig, .base_dir = "."}, cancel);
    REQUIRE(r.has_value());
    return *r;
}

CheckResult check_answer(const Task& t, const std::string& answer, int variant = 1) {
    signals::core::CancelToken cancel;
    auto r = check_task(t, variant, {.answer = answer}, cancel);
    REQUIRE(r.has_value());
    return *r;
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("the solution passes and the start does not", "[learn][checker]") {
    for (int variant : {1, 2, 3}) {
        const auto& dbl = task("1.2");
        const auto good = check_graph(dbl, solution_graph(dbl, variant), variant);
        CHECK(good.passed);
        CHECK_FALSE(good.error.has_value());
        CHECK(good.inputs == 3);
        REQUIRE(good.goals.size() == 3);
        for (const auto& g : good.goals) CHECK(g.passed);
        const auto bad = check_graph(dbl, start_graph(dbl, variant), variant);
        CHECK_FALSE(bad.passed);
        REQUIRE(bad.goals.size() == 3);
        CHECK_FALSE(bad.goals[0].passed);
        CHECK_FALSE(bad.goals[1].passed);
        CHECK_FALSE(bad.goals[2].passed);
        CHECK(check_graph(task("2.2"), solution_graph(task("2.2"), variant), variant).passed);
    }
    CHECK(start_graph(task("1.2"), 1).find("{{") == std::string::npos);
}

TEST_CASE("a cheat that wires x to the output fails the path goal", "[learn][checker]") {
    const auto& bank = task("2.2");
    const auto r = check_graph(bank, start_graph(bank, 1));
    CHECK_FALSE(r.passed);
    REQUIRE(r.goals.size() == 3);
    CHECK_FALSE(r.goals[1].passed);
    CHECK(r.goals[2].passed);
    const auto dwt = check_graph(bank, substitute(bank.cheats[0].second, draw_params(bank.params, bank.id, 1)));
    CHECK_FALSE(dwt.passed);
    CHECK_FALSE(dwt.error.has_value());
    CHECK(dwt.goals[0].passed);
    CHECK_FALSE(dwt.goals[1].passed);
    CHECK_FALSE(dwt.goals[2].passed);
    CHECK(dwt.goals[2].detail.find("dwt") != std::string::npos);
}

TEST_CASE("hidden inputs catch a hard-coded output", "[learn][checker]") {
    const auto& dbl = task("1.2");
    const auto r = check_graph(dbl, substitute(dbl.cheats[0].second, draw_params(dbl.params, dbl.id, 1)));
    CHECK_FALSE(r.passed);
    CHECK_FALSE(r.goals[0].passed);
    CHECK_FALSE(r.goals[0].detail.empty());
    CHECK(r.goals[1].passed);
}

TEST_CASE("a deleted input node is reported", "[learn][checker]") {
    const auto& dbl = task("1.2");
    auto sig = solution_graph(dbl, 1);
    sig = replaced(sig, "node x = samples", "node x2 = samples");
    sig = replaced(sig, "x -> y.a\nx -> y.b", "x2 -> y.a\nx2 -> y.b");
    const auto r = check_graph(dbl, sig);
    CHECK_FALSE(r.passed);
    REQUIRE(r.error.has_value());
    CHECK(r.error->find("x") != std::string::npos);
    CHECK(r.goals.size() == 3);
}

TEST_CASE("a graph that does not build gives an error with a line", "[learn][checker]") {
    const auto& dbl = task("1.2");
    const auto r = check_graph(dbl, "pipeline \"t\" v1\n\nnode x = samples(values=\"1 2\", rate=8)\nnode y = nosuchblock()\nx -> y\n");
    CHECK_FALSE(r.passed);
    REQUIRE(r.error.has_value());
    CHECK(r.error->find("4") != std::string::npos);
    const auto broken = check_graph(dbl, "pipeline \"t\" v1\nnode x = = 1\n");
    REQUIRE(broken.error.has_value());
    CHECK(broken.error->find("2") != std::string::npos);
}

TEST_CASE("answer tasks compare with the expected value from the solution graph", "[learn][checker]") {
    const auto& sum = task("1.1");
    const auto a = std::stoi(draw_params(sum.params, sum.id, 1).at("a"));
    CHECK(check_answer(sum, std::to_string(a + 1)).passed);
    CHECK_FALSE(check_answer(sum, std::to_string(a + 2)).passed);
    const auto junk = check_answer(sum, "abc");
    CHECK_FALSE(junk.passed);
    CHECK(junk.error.has_value());

    const auto& delayed = task("2.1");
    auto expected = expected_answer(delayed, 1);
    REQUIRE(expected.has_value());
    REQUIRE(expected->size() == 5);
    CHECK(expected->front() == 0);
    std::string text;
    for (double v : *expected) text += std::to_string(static_cast<int>(v)) + " ";
    CHECK(check_answer(delayed, text).passed);
    CHECK_FALSE(check_answer(delayed, "0 0 0 0").passed);
}

TEST_CASE("text placeholders take parameters, expected values and reference measures", "[learn][checker]") {
    const auto& sum = task("1.1");
    const auto a = draw_params(sum.params, sum.id, 1).at("a");
    CHECK(render_text("{{a}} -> {{expected}}", sum, 1) == a + " -> " + std::to_string(std::stoi(a) + 1));
    const auto shown = render_text("{{m.err}} {{x}}", task("2.2"), 1);
    CHECK(std::stod(shown.substr(0, shown.find(' '))) < 1e-12);
    CHECK(shown.find("{{") == std::string::npos);
    CHECK(render_text("{{nope}}", sum, 1) == "{{nope}}");
}

TEST_CASE("a cancelled check stops", "[learn][checker]") {
    const auto& dbl = task("1.2");
    signals::core::CancelToken cancel;
    cancel.cancel();
    auto r = check_task(dbl, 1, {.sig = solution_graph(dbl, 1), .base_dir = "."}, cancel);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().code == signals::core::Errc::cancelled);
}

TEST_CASE("a check over the time limit reports a timeout", "[learn][checker]") {
    const auto& dbl = task("1.2");
    signals::core::CancelToken cancel;
    const auto heavy = "pipeline \"t\" v1\nnode x = samples(values=\"1 2\", rate=8)\nnode g = gen(kind=white, rate=48000, duration=60)\n"
                       "node m = moving_average(length=4000)\nnode y = add()\ng -> m\nx -> y.a\nx -> y.b\n";
    auto r = check_task(dbl, 1, {.sig = heavy, .base_dir = "."}, cancel, 0.01);
    REQUIRE(r.has_value());
    CHECK_FALSE(r->passed);
    REQUIRE(r->error.has_value());
    signals::LanguageScope en{signals::Language::en};
    auto again = check_task(dbl, 1, {.sig = heavy, .base_dir = "."}, cancel, 0.01);
    REQUIRE(again.has_value());
    REQUIRE(again->error.has_value());
    CHECK(again->error->find("30") == std::string::npos);
    CHECK(again->error->find("0.01") != std::string::npos);
}
