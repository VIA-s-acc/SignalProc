#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <vector>

#include "core/design/families.hpp"
#include "tui/screens/diagram_model.hpp"

using namespace signals::tui;

namespace {

std::vector<double> noise(std::size_t n, unsigned seed) {
    std::mt19937 rng{seed};
    std::normal_distribution<double> g;
    std::vector<double> x(n);
    for (auto& v : x) v = g(rng);
    return x;
}

double max_gap(const std::vector<double>& a, const std::vector<double>& b) {
    double worst = 0;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i)
        worst = std::max(worst, std::abs((i < a.size() ? a[i] : 0.0) - (i < b.size() ? b[i] : 0.0)));
    return worst;
}

}  // namespace

TEST_CASE("haar diagram wires carry the explain example", "[tui][diagram]") {
    const auto bank = signals::core::wavelet("haar");
    REQUIRE(bank);
    const std::vector<double> x{4, 2, 5, 7, 1, 3, 6, 6};
    const auto r = run_diagram(*bank, x);
    REQUIRE(r);
    const auto* a = r->wire("a");
    const auto* d = r->wire("d");
    REQUIRE(a);
    REQUIRE(d);
    const std::vector<double> expected_a{6 / std::sqrt(2.0), 12 / std::sqrt(2.0), 4 / std::sqrt(2.0), 12 / std::sqrt(2.0)};
    CHECK(max_gap(a->values, expected_a) < 1e-12);
    REQUIRE(d->values.size() == 4);
    CHECK(std::abs(std::abs(d->values[0]) - std::sqrt(2.0)) < 1e-12);
    CHECK(std::abs(d->values[3]) < 1e-12);
    CHECK(r->wire("y0")->values.size() == 9);
    CHECK(r->wire("u0")->values.size() == 8);
    CHECK(r->delay == 0);
    CHECK(r->error < 1e-12);
    CHECK(r->direct_mults == 8);
    CHECK(r->polyphase_mults == 4);
}

TEST_CASE("polyphase wires equal the direct ones", "[tui][diagram]") {
    for (const char* name : {"db4", "bior2.2", "sym5"}) {
        const auto bank = signals::core::wavelet(name);
        REQUIRE(bank);
        for (const std::size_t n : {1, 2, 3, 37, 64}) {
            INFO(name << " n=" << n);
            const auto x = noise(n, 7);
            const auto r = run_diagram(*bank, x);
            REQUIRE(r);
            CHECK(max_gap(r->wire("a")->values, r->wire("a", true)->values) < 1e-12);
            CHECK(max_gap(r->wire("d")->values, r->wire("d", true)->values) < 1e-12);
            CHECK(max_gap(r->wire("xh")->values, r->wire("xh", true)->values) < 1e-12);
            CHECK(r->wire("xe", true)->values.size() == (n + 1) / 2);
            CHECK(r->wire("xo", true)->values.size() == n / 2);
            if (n >= 2) CHECK(r->error < 1e-12);
        }
    }
}

TEST_CASE("the delay of the chain is found from the output", "[tui][diagram]") {
    const auto bank = signals::core::wavelet("db4");
    REQUIRE(bank);
    const auto r = run_diagram(*bank, noise(64, 3));
    REQUIRE(r);
    CHECK(r->delay == static_cast<std::ptrdiff_t>(bank->length()) - 2);
    CHECK(r->error < 1e-12);
    CHECK(r->direct_mults == 32);
    CHECK(r->polyphase_mults == 16);
    CHECK_FALSE(run_diagram(*bank, std::vector<double>{}));
}
