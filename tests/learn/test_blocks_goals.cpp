#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "i18n/i18n.hpp"
#include "learn/checker.hpp"
#include "learn/task.hpp"
#include "learn/variant.hpp"
#include "support/paths.hpp"

using namespace signals::learn;
using Catch::Matchers::ContainsSubstring;

namespace {

const Task& bank() {
    static const Course c = [] {
        auto files = read_course_dir(signals::test::learn_fixture("mini"));
        REQUIRE(files.has_value());
        auto course = load_course(*files);
        REQUIRE(course.has_value());
        return *course;
    }();
    const auto* t = c.find("2.2");
    REQUIRE(t != nullptr);
    return *t;
}

CheckResult check_graph(const Task& t, const std::string& sig) {
    signals::core::CancelToken cancel;
    auto r = check_task(t, 1, {.sig = sig, .base_dir = "."}, cancel);
    REQUIRE(r.has_value());
    return *r;
}

std::string values_of(const Task& t) {
    const auto start = start_graph(t, 1);
    const auto at = start.find("values=\"") + 8;
    return start.substr(at, start.find('"', at) - at);
}

std::string with_block(std::string sig, const std::string& block, const std::string& tail) {
    const auto header = sig.find('\n') + 1;
    sig.insert(header, block);
    return sig + tail;
}

constexpr const char* kAmp = "block amp(db=0) {\n    in x\n    out y\n    node g = gain(db=$db)\n    node sum = add()\n"
                             "    x -> g -> sum.a\n    x -> sum.b\n    sum -> y\n}\n";

Task calls_task() {
    Task t;
    t.id = "b.1";
    t.start_sig = "pipeline \"t\" v2\nnode x = samples(values=\"1 2 3 4 5 6 7 8\", rate=8)\n";
    t.solution_sig = t.start_sig;
    t.locked = {"x"};
    t.measures = {Measure{.name = "e", .kind = "energy", .a = "lv2/sum"}, Measure{.name = "n", .kind = "count", .block = "gain"},
                  Measure{.name = "p", .kind = "param", .param = "lv1/g.db"}};
    t.goals = {Goal{.when = "e > 0 && n == 2 && p == -6"}, Goal{.require = {{"gain", 2}}}, Goal{.defines = 2}, Goal{.variable = 2}};
    return t;
}

}  // namespace

TEST_CASE("a forbidden block hidden in an own block is caught", "[learn][blocks-goals]") {
    const auto& t = bank();
    const auto sig = with_block(solution_graph(t, 1), "block hide {\n    in x\n    out y\n    node t = dwt(wavelet=haar, levels=1)\n    node i = idwt()\n    x -> t -> i -> y\n}\n",
                                "node hid = hide()\nx -> hid\n");
    const auto r = check_graph(t, sig);
    INFO(r.error.value_or(""));
    CHECK_FALSE(r.error.has_value());
    REQUIRE(r.goals.size() == 3);
    CHECK(r.goals[0].passed);
    CHECK_FALSE(r.goals[2].passed);
    CHECK_THAT(r.goals[2].detail, ContainsSubstring("dwt"));
}

TEST_CASE("through follows a path inside an own block", "[learn][blocks-goals]") {
    const auto& t = bank();
    const std::string level = "block level {\n    in x\n    out ya, yb\n"
                              "    node h0 = convolve(filter=haar.h0, method=direct)\n    node h1 = convolve(filter=haar.h1, method=direct)\n"
                              "    node a = downsample(m=2, anti_alias=no, phase=1)\n    node d = downsample(m=2, anti_alias=no, phase=1)\n"
                              "    node ua = upsample(m=2, anti_image=no)\n    node ud = upsample(m=2, anti_image=no)\n"
                              "    node f0 = convolve(filter=haar.f0, method=direct)\n    node f1 = convolve(filter=haar.f1, method=direct)\n"
                              "    x -> h0 -> a -> ua -> f0 -> ya\n    x -> h1 -> d -> ud -> f1 -> yb\n}\n";
    const auto sig = "pipeline \"bank\" v2\n" + level + "node x = samples(values=\"" + values_of(t) + "\", rate=8)\nnode lv = level()\nnode sum = add()\n" +
                     "x -> lv\nlv.ya -> sum.a\nlv.yb -> sum.b\n";
    const auto r = check_graph(t, sig);
    INFO(r.error.value_or(""));
    CHECK(r.passed);
}

TEST_CASE("measures, require, defines and variable see calls of own blocks", "[learn][blocks-goals]") {
    const auto t = calls_task();
    const auto good = check_graph(t, std::string{"pipeline \"t\" v2\nlet level = -6\n"} + kAmp +
                                         "node x = samples(values=\"1 2 3 4 5 6 7 8\", rate=8)\nnode lv1 = amp(db=$level)\nnode lv2 = amp(db=$level)\nx -> lv1 -> lv2\n");
    INFO(good.error.value_or(""));
    REQUIRE(good.goals.size() == 4);
    for (const auto& g : good.goals) {
        INFO(g.detail);
        CHECK(g.passed);
    }
    Task few = t;
    few.measures.clear();
    few.goals = {Goal{.defines = 2}, Goal{.variable = 2}};
    const auto bad = check_graph(few, std::string{"pipeline \"t\" v2\nlet level = -6\n"} + kAmp +
                                          "node x = samples(values=\"1 2 3 4 5 6 7 8\", rate=8)\nnode lv1 = amp(db=$level)\nnode g2 = gain(db=-6)\nx -> lv1 -> g2\n");
    REQUIRE(bad.goals.size() == 2);
    CHECK_FALSE(bad.goals[0].passed);
    CHECK_THAT(bad.goals[0].detail, ContainsSubstring("1"));
    CHECK_FALSE(bad.goals[1].passed);
    CHECK_FALSE(bad.goals[1].detail.empty());
}

TEST_CASE("a locked node moved into a block is reported as moved", "[learn][blocks-goals]") {
    const auto& t = bank();
    const auto values = values_of(t);
    const auto moved = check_graph(t, "pipeline \"bank\" v2\nblock src {\n    out y\n    node x = samples(values=\"" + values +
                                          "\", rate=8)\n    x -> y\n}\nnode s = src()\nnode sum = add()\ns -> sum.a\ns -> sum.b\n");
    CHECK_FALSE(moved.passed);
    REQUIRE(moved.error.has_value());
    auto expected = signals::tr("learn.check.locked_moved");
    expected = expected.substr(0, expected.find('{'));
    CHECK_THAT(*moved.error, ContainsSubstring(expected));
    CHECK_THAT(*moved.error, ContainsSubstring("x"));
    const auto same = check_graph(t, with_block(start_graph(t, 1), "let r = 8\n", ""));
    CHECK_FALSE(same.error.has_value());
    auto via = start_graph(t, 1);
    via.replace(via.find("rate=8"), 6, "rate=$r");
    const auto kept = check_graph(t, with_block(via, "let r = 8\n", ""));
    INFO(kept.error.value_or(""));
    CHECK_FALSE(kept.error.has_value());
    const auto changed = check_graph(t, with_block(via, "let r = 16\n", ""));
    REQUIRE(changed.error.has_value());
    CHECK_THAT(*changed.error, ContainsSubstring("x"));
}

TEST_CASE("defines and variable goals are read from task.toml", "[learn][blocks-goals]") {
    auto files = read_course_dir(signals::test::learn_fixture("mini"));
    REQUIRE(files.has_value());
    const std::string dir = "02-more/02-bank/";
    (*files)[dir + "task.toml"] += "\n[[goal]]\ndefines = { calls = 2 }\n\n[[goal]]\nvariable = { uses = 3 }\n";
    for (const auto* lang : {"ru.md", "en.md"}) {
        auto& text = (*files)[dir + lang];
        const auto at = text.find("\n3. ");
        REQUIRE(at != std::string::npos);
        const auto end = text.find('\n', at + 1);
        text.insert(end + 1, "4. Block.\n5. Variable.\n");
    }
    const auto course = load_course(*files);
    INFO((course ? std::string{} : course.error().message));
    REQUIRE(course);
    const auto* t = course->find("2.2");
    REQUIRE(t);
    REQUIRE(t->goals.size() == 5);
    CHECK(t->goals[3].defines == 2);
    CHECK(t->goals[4].variable == 3);
    (*files)[dir + "task.toml"] += "\n[[goal]]\ndefines = { calls = 2 }\nprobe = \"a\"\n";
    CHECK_FALSE(load_course(*files));
}
