#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/metrics.hpp"
#include "learn/measure.hpp"
#include "learn_support.hpp"

using namespace signals::learn;
using signals::test::run_doc;
using signals::test::sig_doc;

namespace {

double measure(const signals::pipeline::PipelineDoc& base, Measure m, const signals::pipeline::RunResult* reference = nullptr) {
    auto doc = base;
    add_measure_probes(doc, std::vector<Measure>{m});
    const auto run = run_doc(doc);
    auto v = compute_measure(m, doc, run, reference);
    REQUIRE(v.has_value());
    return v->scalar();
}

const char* kDelayed = R"(pipeline "t" v1
node x = samples(values="1 -2 3 4 -5 6 7 -8", rate=8)
node y = delay(samples=2)
x -> y
)";

}  // namespace

TEST_CASE("max error aligns a fixed and an automatic delay", "[learn][measure]") {
    const auto doc = sig_doc(kDelayed);
    CHECK(measure(doc, {.name = "e", .kind = "max_error", .a = "y", .b = "x", .delay = "2"}) == 0);
    CHECK(measure(doc, {.name = "e", .kind = "max_error", .a = "y", .b = "x", .delay = "auto"}) == 0);
    CHECK(measure(doc, {.name = "e", .kind = "max_error", .a = "y", .b = "x"}) == 14);
    CHECK(measure(doc, {.name = "e", .kind = "max_error", .a = "y", .b = "x", .trim = 3}) == 8);
}

TEST_CASE("delay finds the lag of the correlation peak", "[learn][measure]") {
    CHECK(measure(sig_doc(kDelayed), {.name = "d", .kind = "delay", .a = "y", .b = "x"}) == 2);
}

TEST_CASE("snr matches the metrics function", "[learn][measure]") {
    const auto doc = sig_doc(R"(pipeline "t" v1
node x = samples(values="1 2 3 4 5 6 7 8", rate=8)
node n = samples(values="0.01 -0.02 0 0.03 0 0 -0.01 0", rate=8)
node y = add()
x -> y.a
n -> y.b
)");
    const std::vector<double> x{1, 2, 3, 4, 5, 6, 7, 8}, y{1.01, 1.98, 3, 4.03, 5, 6, 6.99, 8};
    CHECK(measure(doc, {.name = "s", .kind = "snr", .a = "y", .b = "x"}) == Catch::Approx(signals::core::snr_db(x, y)).epsilon(1e-12));
}

TEST_CASE("tone level of a half-amplitude sine is -6.02 dB", "[learn][measure]") {
    const auto doc = sig_doc(R"(pipeline "t" v1
node s = gen(kind=sine, freq=440, amplitude=0.5, rate=8000, duration=1)
node g = gain(db=-6)
s -> g
)");
    CHECK(measure(doc, {.name = "t", .kind = "tone_db", .a = "s", .freq = 440}) == Catch::Approx(-6.0206).margin(0.01));
    CHECK(measure(doc, {.name = "g", .kind = "gain_db", .a = "g", .b = "s", .freq = 440}) == Catch::Approx(-6).margin(0.01));
    CHECK(measure(doc, {.name = "t", .kind = "tone_db", .a = "s", .freq = 1000}) < -60);
}

TEST_CASE("signal sizes and levels", "[learn][measure]") {
    const auto doc = sig_doc(kDelayed);
    CHECK(measure(doc, {.name = "m", .kind = "max_abs", .a = "x"}) == 8);
    CHECK(measure(doc, {.name = "e", .kind = "energy", .a = "x"}) == 204);
    CHECK(measure(doc, {.name = "n", .kind = "length", .a = "y"}) == 10);
    CHECK(measure(doc, {.name = "r", .kind = "rms_db", .a = "x"}) == Catch::Approx(20 * std::log10(std::sqrt(204.0 / 8))));
}

TEST_CASE("kept counts nonzero coefficients", "[learn][measure]") {
    const auto doc = sig_doc(R"(pipeline "t" v1
node x = samples(values="1 0 0 0 0 0 0 0", rate=8)
node w = dwt(wavelet=haar, levels=1, ext=periodization)
x -> w
)");
    CHECK(measure(doc, {.name = "k", .kind = "kept", .a = "w"}) == 0.25);
}

TEST_CASE("report fields, samples, counts and params", "[learn][measure]") {
    const auto doc = sig_doc(R"(pipeline "t" v1
node x = samples(values="1 2 3", rate=8)
node y = samples(values="1 2 4", rate=8)
node c = check(kind=error, tol=10)
x -> c.ref
y -> c.in
)");
    CHECK(measure(doc, {.name = "f", .kind = "field", .a = "c", .field = "max_abs"}) == 1);
    auto m = Measure{.name = "s", .kind = "samples", .a = "y"};
    auto d = doc;
    add_measure_probes(d, std::vector<Measure>{m});
    auto v = compute_measure(m, d, run_doc(d));
    REQUIRE(v.has_value());
    CHECK(v->values == std::vector<double>{1, 2, 4});
    CHECK(measure(doc, {.name = "n", .kind = "count", .block = "samples"}) == 2);
    CHECK(measure(doc, {.name = "p", .kind = "param", .param = "c.tol"}) == 10);
}

TEST_CASE("the reference output comes from the solution run", "[learn][measure]") {
    auto solution = sig_doc(R"(pipeline "t" v1
node x = samples(values="1 2 3", rate=8)
node y = gain(db=0)
x -> y
)");
    const Measure m{.name = "e", .kind = "max_error", .a = "y", .ref = "y"};
    add_measure_probes(solution, std::vector<Measure>{m}, true);
    const auto reference = run_doc(solution);
    auto work = sig_doc(R"(pipeline "t" v1
node x = samples(values="1 2 3", rate=8)
node y = delay(samples=0)
x -> y
)");
    CHECK(measure(work, m, &reference) == 0);
}

TEST_CASE("a too short output gives infinite error", "[learn][measure]") {
    const auto doc = sig_doc(R"(pipeline "t" v1
node x = samples(values="1 2 3 4 5 6 7 8", rate=8)
node y = cut(begin=0, end=4)
x -> y
)");
    CHECK(std::isinf(measure(doc, {.name = "e", .kind = "max_error", .a = "y", .b = "x"})));
    CHECK(measure(doc, {.name = "s", .kind = "snr", .a = "y", .b = "x"}) == -std::numeric_limits<double>::infinity());
}

TEST_CASE("a missing output is an error, not a number", "[learn][measure]") {
    const auto doc = sig_doc(kDelayed);
    const Measure m{.name = "e", .kind = "max_error", .a = "nope", .b = "x"};
    auto d = doc;
    add_measure_probes(d, std::vector<Measure>{Measure{.name = "e", .kind = "max_error", .a = "y", .b = "x"}});
    CHECK_FALSE(compute_measure(m, d, run_doc(d)).has_value());
}
