#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

#include "core/design/elliptic.hpp"
#include "core/design/iir.hpp"
#include "support/golden.hpp"

using namespace signals::core;
using C = std::complex<double>;

namespace {

std::vector<C> roots_of(const nlohmann::json& j) {
    std::vector<C> out;
    for (const auto& v : j) out.emplace_back(v[0].get<double>(), v[1].get<double>());
    return out;
}

double match_error(std::vector<C> expected, std::vector<C> actual) {
    if (expected.size() != actual.size()) return INFINITY;
    double worst = 0;
    for (const auto& e : expected) {
        auto best = actual.begin();
        for (auto it = actual.begin(); it != actual.end(); ++it)
            if (std::abs(*it - e) < std::abs(*best - e)) best = it;
        worst = std::max(worst, std::abs(*best - e) / std::max(1.0, std::abs(e)));
        actual.erase(best);
    }
    return worst;
}

IirFamily family_of(const std::string& name) { return *parse_iir_family(name); }

}  // namespace

TEST_CASE("elliptic integrals and Jacobi functions match scipy", "[iir][golden]") {
    const auto g = signals::test::load_golden("iir.json");
    for (const auto& c : g["special"]["ellipk"]) {
        const double m = c["m"];
        INFO("m " << m);
        CHECK(std::abs(ellipk(m) - c["k"].get<double>()) <= 1e-13 * c["k"].get<double>());
        if (!c["km1"].is_null()) CHECK(std::abs(ellipkm1(1 - m) - c["km1"].get<double>()) <= 1e-13 * c["km1"].get<double>());
    }
    for (const auto& c : g["special"]["ellipj"]) {
        INFO("u " << c["u"].get<double>() << " m " << c["m"].get<double>());
        const auto j = ellipj(c["u"], c["m"]);
        CHECK(std::abs(j.sn - c["sn"].get<double>()) <= 1e-13);
        CHECK(std::abs(j.cn - c["cn"].get<double>()) <= 1e-13);
        CHECK(std::abs(j.dn - c["dn"].get<double>()) <= 1e-13);
    }
}

TEST_CASE("analog prototypes match scipy", "[iir][golden]") {
    const auto g = signals::test::load_golden("iir.json");
    for (const auto& c : g["prototypes"]) {
        const auto name = c["family"].get<std::string>();
        const int n = c["n"];
        INFO(name << " n=" << n);
        const auto f = analog_prototype(family_of(name), n, name == "ellip" || name == "cheby1" ? 1.0 : 0, name == "ellip" || name == "cheby2" ? 40.0 : 0);
        REQUIRE(f);
        CHECK(match_error(roots_of(c["z"]), f->z) <= 1e-9);
        CHECK(match_error(roots_of(c["p"]), f->p) <= 1e-9);
        CHECK(std::abs(f->k - c["k"].get<double>()) <= 1e-9 * std::max(1.0, std::abs(c["k"].get<double>())));
    }
}

TEST_CASE("digital iir filters and their sos match scipy", "[iir][golden]") {
    const auto g = signals::test::load_golden("iir.json");
    std::vector<double> w(512);
    for (std::size_t i = 0; i < w.size(); ++i) w[i] = std::numbers::pi * static_cast<double>(i) / 512;
    for (const auto& c : g["digital"]) {
        const auto name = c["family"].get<std::string>();
        IirSpec s;
        s.family = family_of(name);
        s.type = *parse_band_type(c["type"].get<std::string>());
        s.order = c["n"];
        s.wn = c["wn"].get<std::vector<double>>();
        if (!c["rp"].is_null()) s.rp = c["rp"];
        if (!c["rs"].is_null()) s.rs = c["rs"];
        INFO(name << " " << c["type"].get<std::string>() << " n=" << s.order);
        const auto f = iir_design(s);
        REQUIRE(f);
        CHECK(match_error(roots_of(c["z"]), f->z) <= 1e-8);
        CHECK(match_error(roots_of(c["p"]), f->p) <= 1e-8);
        CHECK(std::abs(f->k - c["k"].get<double>()) <= 1e-8 * std::max(1e-12, std::abs(c["k"].get<double>())));
        CHECK(is_stable(*f));
        const auto sos = zpk_to_sos(*f);
        const auto h = sos_response(sos, w);
        const auto expected = c["mag"].get<std::vector<double>>();
        double worst = 0;
        for (std::size_t i = 0; i < w.size(); ++i) worst = std::max(worst, std::abs(std::abs(h[i]) - expected[i]));
        CHECK(worst <= 1e-9);
    }
}

TEST_CASE("iir specs are validated and instability is detected", "[iir]") {
    IirSpec s;
    s.wn = {1.2};
    CHECK_FALSE(iir_design(s));
    s.wn = {0.3, 0.2};
    s.type = BandType::bandpass;
    CHECK_FALSE(iir_design(s));
    s.order = 0;
    s.type = BandType::lowpass;
    s.wn = {0.3};
    CHECK_FALSE(iir_design(s));
    Zpk bad;
    bad.p = {C{1.01, 0}};
    CHECK_FALSE(is_stable(bad));
}
