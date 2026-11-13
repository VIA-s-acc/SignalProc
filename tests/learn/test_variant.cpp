#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "learn/variant.hpp"

using namespace signals::learn;

TEST_CASE("the generator gives the same numbers on every platform", "[learn][variant]") {
    Rng r{1};
    CHECK(r.next() == 10451216379200822465ULL);
    CHECK(r.next() == 13757245211066428519ULL);
    CHECK(r.next() == 17911839290282890590ULL);
    CHECK(seed_of("4.1", 1) == 2449556640903290949ULL);

    const std::vector<ParamSpec> specs{{"x", ParamSpec::Kind::ints, 8, -9, 9}};
    CHECK(draw_params(specs, "4.1", 1).at("x") == "8 6 -6 0 4 0 -1 -7");
    CHECK(draw_params(specs, "4.1", 2).at("x") == "3 -9 -8 -1 0 0 -7 -8");

    const std::vector<ParamSpec> real{{"a", ParamSpec::Kind::real, 1, 0.5, 2.0, 2}};
    CHECK(draw_params(real, "x", 1).at("a") == "1.39");
}

TEST_CASE("variants differ and repeat", "[learn][variant]") {
    const std::vector<ParamSpec> specs{{"n", ParamSpec::Kind::integer, 1, 1, 1000}, {"w", ParamSpec::Kind::choice, 1, 0, 0, 0, {"haar", "db2", "db4"}}};
    CHECK(draw_params(specs, "2.3", 5) == draw_params(specs, "2.3", 5));
    CHECK(draw_params(specs, "2.3", 5) != draw_params(specs, "2.3", 6));
    const auto w = draw_params(specs, "2.3", 5).at("w");
    CHECK((w == "haar" || w == "db2" || w == "db4"));
    Rng r{7};
    for (int i = 0; i < 1000; ++i) {
        const auto v = r.range(-3, 3);
        CHECK((v >= -3 && v <= 3));
        const auto u = r.uniform();
        CHECK((u >= 0 && u < 1));
    }
}

TEST_CASE("substitution keeps unknown placeholders", "[learn][variant]") {
    const ParamValues v{{"x", "1 2"}, {"n", "4"}};
    CHECK(substitute("x = [{{x}}], n = {{n}}, m = {{m}}", v) == "x = [1 2], n = 4, m = {{m}}");
    CHECK(placeholders("{{x}} and {{ m.err }} and {{x}}") == std::vector<std::string>{"x", "m.err"});
}
