#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/extension.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("pad matches pywt.pad for all golden cases", "[extension][golden]") {
    const auto golden = signals::test::load_golden("pad.json");
    for (auto& c : golden["cases"]) {
        auto mode = parse_extension(c["mode"].get<std::string>());
        REQUIRE(mode);
        auto x = c["x"].get<std::vector<double>>();
        auto y = pad<double>(x, c["before"].get<std::size_t>(), c["after"].get<std::size_t>(), *mode);
        INFO(c["mode"] << " n=" << x.size() << " before=" << c["before"] << " after=" << c["after"]);
        CHECK(y == c["y"].get<std::vector<double>>());
    }
}

TEST_CASE("coefficient lengths follow pywt formulas", "[extension]") {
    CHECK(dwt_coeff_len(1000, 8, Extension::symmetric) == 503);
    CHECK(dwt_coeff_len(17, 2, Extension::periodization) == 9);
    CHECK(dwt_coeff_len(1, 62, Extension::zero) == 31);
}

TEST_CASE("unknown extension name is rejected", "[extension]") {
    CHECK_FALSE(parse_extension("symh"));
    CHECK(to_string(Extension::antireflect) == "antireflect");
}
