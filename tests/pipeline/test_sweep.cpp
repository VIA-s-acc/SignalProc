#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "pipeline/sig.hpp"
#include "pipeline/sweep.hpp"

using namespace signals;
using namespace signals::pipeline;
using Catch::Matchers::WithinAbs;

namespace {

PipelineDoc doc_of(std::string_view text) {
    auto doc = parse_sig(text);
    REQUIRE(doc);
    return *doc;
}

constexpr std::string_view kGain = R"(node x = samples(values="1 -2 3 -4", rate=8)
node g = gain(db=0)
node s = stats()
x -> g -> s
)";

}  // namespace

TEST_CASE("vary accepts a step range, a count, a log count and a list", "[pipeline][sweep]") {
    const auto step = parse_vary("g.db=0:1:0.25");
    REQUIRE(step);
    CHECK(step->node == "g");
    CHECK(step->param == "db");
    CHECK(step->values == std::vector<std::string>{"0", "0.25", "0.5", "0.75", "1"});
    const auto count = parse_vary("g.db=-6:6@3");
    REQUIRE(count);
    CHECK(count->values == std::vector<std::string>{"-6", "0", "6"});
    const auto log = parse_vary("f.freq=10:1000@3log");
    REQUIRE(log);
    CHECK(log->values == std::vector<std::string>{"10", "100", "1000"});
    const auto list = parse_vary("thr.mode=soft,hard");
    REQUIRE(list);
    CHECK(list->values == std::vector<std::string>{"soft", "hard"});
    CHECK_FALSE(parse_vary("g.db"));
    CHECK_FALSE(parse_vary("g.db=0:1:0"));
    CHECK_FALSE(parse_vary("g.db=1:100@3log0"));
    CHECK_FALSE(parse_vary("g.db=0:1000000:0.001"));
    const auto huge = parse_vary("g.db=0:1@100000000000000");
    REQUIRE_FALSE(huge);
    CHECK(huge.error().message.find("100000000000000") != std::string::npos);
    CHECK(parse_vary("g.db=0:1000000:0.001").error().message.find("1000000001") != std::string::npos);
}

TEST_CASE("measures name a report field or a metric of a node output", "[pipeline][sweep]") {
    const auto field = parse_measure("s.rms_ch0");
    REQUIRE(field);
    CHECK(field->node == "s");
    CHECK(field->field == "rms_ch0");
    CHECK_FALSE(field->metric);
    const auto metric = parse_measure("g:peak");
    REQUIRE(metric);
    CHECK(metric->node == "g");
    CHECK(metric->metric == Metric::peak);
    CHECK_FALSE(parse_measure("g:loudest"));
    CHECK_FALSE(parse_measure("nothing"));
}

TEST_CASE("a sweep runs the graph once per value and collects every measure", "[pipeline][sweep]") {
    const auto doc = doc_of(kGain);
    auto axis = parse_vary("g.db=0,6.0206,bad");
    REQUIRE(axis);
    std::vector<Measure> measures{*parse_measure("g:peak"), *parse_measure("s.peak"), *parse_measure("g:rms"), *parse_measure("g:energy")};
    core::CancelToken cancel;
    std::vector<std::size_t> seen;
    const auto r = run_sweep(doc, ".", *axis, measures, cancel, [&](std::size_t done, std::size_t total) {
        CHECK(total == 3);
        seen.push_back(done);
    });
    REQUIRE(r);
    REQUIRE(r->rows.size() == 3);
    CHECK(seen.back() == 3);
    CHECK_THAT(*r->rows[0].results[0], WithinAbs(4, 1e-12));
    CHECK_THAT(*r->rows[1].results[0], WithinAbs(8, 1e-4));
    REQUIRE(r->rows[1].results[1]);
    CHECK_THAT(*r->rows[1].results[1], WithinAbs(8, 1e-4));
    CHECK_THAT(*r->rows[0].results[2], WithinAbs(std::sqrt(30.0 / 4), 1e-12));
    CHECK_THAT(*r->rows[0].results[3], WithinAbs(30, 1e-12));
    CHECK_FALSE(r->rows[2].error.empty());
    CHECK_FALSE(r->rows[2].results[0]);
}

TEST_CASE("a sweep refuses an unknown node or parameter before running", "[pipeline][sweep]") {
    const auto doc = doc_of(kGain);
    core::CancelToken cancel;
    CHECK_FALSE(run_sweep(doc, ".", *parse_vary("h.db=0,1"), {*parse_measure("g:peak")}, cancel));
    CHECK_FALSE(run_sweep(doc, ".", *parse_vary("g.gain=0,1"), {*parse_measure("g:peak")}, cancel));
    CHECK_FALSE(run_sweep(doc, ".", *parse_vary("g.db=0,1"), {*parse_measure("q:peak")}, cancel));
}

TEST_CASE("a sweep skips blocks that write files unless a measure reads them", "[pipeline][sweep]") {
    const auto dir = std::filesystem::temp_directory_path() / "signals-sweep-sinks";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto doc = doc_of(R"(node x = samples(values="1 -2 3 -4", rate=8)
node g = gain(db=0)
node e = export(path="out.csv")
node w = wav_out(path="out.wav")
x -> g
g -> e
g -> w
)");
    core::CancelToken cancel;
    const auto r = run_sweep(doc, dir, *parse_vary("g.db=0,6"), {*parse_measure("g:peak")}, cancel);
    REQUIRE(r);
    CHECK(r->rows[1].error.empty());
    CHECK_FALSE(std::filesystem::exists(dir / "out.csv"));
    CHECK_FALSE(std::filesystem::exists(dir / "out.wav"));
    const auto kept = run_sweep(doc, dir, *parse_vary("g.db=0"), {*parse_measure("e.bytes")}, cancel);
    REQUIRE(kept);
    CHECK(std::filesystem::exists(dir / "out.csv"));
}

