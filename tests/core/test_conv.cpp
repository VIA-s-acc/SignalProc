#include <cmath>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/conv.hpp"

using signals::core::ConvMethod;
using signals::core::Laurent;

TEST_CASE("fft convolution matches direct", "[conv]") {
    std::mt19937_64 rng(7);
    std::normal_distribution<double> g;
    Laurent a, b;
    a.offset = 3;
    b.offset = -5;
    for (int i = 0; i < 1000; ++i) a.coeffs.push_back(g(rng));
    for (int i = 0; i < 200; ++i) b.coeffs.push_back(g(rng));
    auto d = convolve(a, b, ConvMethod::direct), f = convolve(a, b, ConvMethod::fft);
    REQUIRE(d.offset == f.offset);
    REQUIRE(d.coeffs.size() == f.coeffs.size());
    for (std::size_t i = 0; i < d.coeffs.size(); ++i) CHECK(std::abs(d.coeffs[i] - f.coeffs[i]) < 1e-11);
}

TEST_CASE("circular convolution wraps by absolute index", "[conv]") {
    std::vector<double> x{1, 2, 3, 4};
    CHECK(circular_convolve(x, Laurent{{1}, 5}) == std::vector<double>{4, 1, 2, 3});
}

TEST_CASE("long convolution runs in linear memory", "[conv]") {
    Laurent x{std::vector<double>(2'000'000, 1.0), 0}, h{{0.5, 0.5}, 0};
    auto y = convolve(x, h);
    CHECK(y.coeffs.size() == 2'000'001);
    CHECK(y.coeffs[1000] == 1.0);
}
