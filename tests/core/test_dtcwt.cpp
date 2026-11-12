#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <string>
#include <vector>

#include "core/dtcwt.hpp"
#include "support/golden.hpp"

using namespace signals::core;

namespace {

double worst(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return INFINITY;
    double w = 0;
    for (std::size_t k = 0; k < a.size(); ++k) w = std::max(w, std::abs(a[k] - b[k]));
    return w;
}

}  // namespace

TEST_CASE("the DTCWT matches the dtcwt package level by level and inverts like it", "[dtcwt][golden]") {
    const auto g = signals::test::load_golden("dtcwt.json");
    for (const auto& c : g["cases"]) {
        INFO(c["n"].get<int>() << " samples, " << c["levels"].get<int>() << " levels, " << c["biort"].get<std::string>() << " + " << c["qshift"].get<std::string>());
        const auto* b = dtcwt::find_biort(c["biort"].get<std::string>());
        const auto* q = dtcwt::find_qshift(c["qshift"].get<std::string>());
        REQUIRE(b);
        REQUIRE(q);
        const auto x = c["x"].get<std::vector<double>>();
        const auto p = dtcwt::forward(x, c["levels"].get<std::size_t>(), *b, *q);
        REQUIRE(p);
        CHECK(worst(p->lowpass, c["lowpass"].get<std::vector<double>>()) < 1e-12);
        REQUIRE(p->highpasses.size() == c["highpasses"].size());
        for (std::size_t level = 0; level < p->highpasses.size(); ++level) {
            INFO("level " << level + 1);
            std::vector<double> re, im;
            for (const auto& z : p->highpasses[level]) re.push_back(z.real()), im.push_back(z.imag());
            CHECK(worst(re, c["highpasses"][level]["re"].get<std::vector<double>>()) < 1e-12);
            CHECK(worst(im, c["highpasses"][level]["im"].get<std::vector<double>>()) < 1e-12);
        }
        const auto y = dtcwt::inverse(*p, *b, *q);
        REQUIRE(y);
        CHECK(worst(*y, c["inverse"].get<std::vector<double>>()) < 1e-12);
        CHECK(worst(*y, x) < 1e-12);
    }
}

TEST_CASE("the DTCWT reconstructs a long random signal to 1e-12 with every filter pair", "[dtcwt]") {
    std::mt19937_64 rng{42};
    std::normal_distribution<double> n01;
    std::vector<double> x(1000);
    for (auto& v : x) v = n01(rng);
    for (const auto& b : dtcwt::biort_table())
        for (const auto& q : dtcwt::qshift_table()) {
            INFO(b.name << " + " << q.name);
            const auto p = dtcwt::forward(x, 6, b, q);
            REQUIRE(p);
            CHECK(p->highpasses.size() == 6);
            const auto y = dtcwt::inverse(*p, b, q);
            REQUIRE(y);
            CHECK(worst(*y, x) < 1e-12);
        }
}

TEST_CASE("the DTCWT refuses an odd length and a broken pyramid, and zero levels keep the signal", "[dtcwt]") {
    const auto& b = dtcwt::biort_table().front();
    const auto& q = dtcwt::qshift_table().front();
    CHECK_FALSE(dtcwt::forward(std::vector<double>{1, 2, 3}, 1, b, q));
    CHECK_FALSE(dtcwt::forward(std::vector<double>{}, 1, b, q));
    const auto none = dtcwt::forward(std::vector<double>{1, 2, 3, 4}, 0, b, q);
    REQUIRE(none);
    CHECK(none->lowpass == std::vector<double>{1, 2, 3, 4});
    CHECK(none->highpasses.empty());
    auto p = dtcwt::forward(std::vector<double>(64, 1.0), 3, b, q);
    REQUIRE(p);
    p->highpasses[1].pop_back();
    CHECK_FALSE(dtcwt::inverse(*p, b, q));
    CHECK(dtcwt::find_biort("near_sym_a"));
    CHECK(dtcwt::find_qshift("qshift_b"));
    CHECK_FALSE(dtcwt::find_biort("qshift_b"));
}
