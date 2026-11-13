#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "commands/cli.hpp"
#include "learn/learn_support.hpp"
#include "support/paths.hpp"

using signals::test::sig_doc;

namespace {

const char* kGraph = R"(pipeline "t" v1
node x = samples(values="1 2 3", rate=8)
node y = samples(values="1 2 3.25", rate=8)
node c = check(kind=error, tol=10)
node s = check(kind=snr, tol=0)
node g = assert(expr="EXPR")
x -> c.ref
y -> c.in
x -> s.ref
y -> s.in
c -> g.a
s -> g.b
)";

std::string graph(const std::string& expr) {
    std::string text = kGraph;
    text.replace(text.find("EXPR"), 4, expr);
    return text;
}

const signals::pipeline::Report* report_of(const signals::pipeline::RunResult& r, std::string_view node) {
    for (const auto& n : r.reports)
        if (n.node == node) return &n.report;
    return nullptr;
}

}  // namespace

TEST_CASE("assert passes and fails by expression", "[pipeline][assert]") {
    auto ok = signals::test::run_doc(sig_doc(graph("a.max_abs <= 0.5 && b.snr_db > 10")));
    REQUIRE(report_of(ok, "g"));
    CHECK(report_of(ok, "g")->passed);
    CHECK(ok.checks_passed);
    auto bad = signals::test::run_doc(sig_doc(graph("a.max_abs < 0.1")));
    CHECK_FALSE(report_of(bad, "g")->passed);
    CHECK_FALSE(bad.checks_passed);
    auto passed_flag = signals::test::run_doc(sig_doc(graph("a.passed == 1 && b.passed")));
    CHECK(report_of(passed_flag, "g")->passed);
}

TEST_CASE("assert names the nearest field when a name is unknown", "[pipeline][assert]") {
    auto graph_doc = sig_doc(graph("a.max_ab < 1"));
    auto built = signals::pipeline::build_graph(graph_doc, ".");
    REQUIRE(built.has_value());
    signals::pipeline::ResultCache cache{0};
    signals::core::CancelToken cancel;
    auto r = signals::pipeline::run_graph(*built, cache, {}, cancel);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("a.max_abs") != std::string::npos);
    auto syntax = signals::pipeline::build_graph(sig_doc(graph("a.max_abs <")), ".");
    REQUIRE(syntax.has_value());
    auto s = signals::pipeline::run_graph(*syntax, cache, {}, cancel);
    REQUIRE_FALSE(s.has_value());
}

TEST_CASE("a failed assert makes signals run exit with 3", "[pipeline][assert][cli]") {
    const auto dir = signals::test::temp_path("assert-cli");
    std::filesystem::create_directories(dir);
    const auto file = (dir / "a.sig").string();
    std::ofstream{file} << graph("a.max_abs < 0.1");
    std::vector<std::string_view> args{"signals", "run", file};
    std::ostringstream out, err;
    CHECK(signals::cli::run_cli(args, out, err) == 3);
    std::ofstream{file} << graph("a.max_abs < 1");
    std::ostringstream out2, err2;
    CHECK(signals::cli::run_cli(args, out2, err2) == 0);
}
