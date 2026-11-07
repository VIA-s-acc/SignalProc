#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "core/design/fir.hpp"
#include "core/metrics.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("remez matches scipy", "[fir][golden]") {
    const auto g = signals::test::load_golden("fir.json");
    for (const auto& c : g["remez"]) {
        const auto type = c["type"].get<std::string>();
        INFO(type << " numtaps=" << c["numtaps"].get<int>());
        RemezSpec s;
        s.numtaps = c["numtaps"];
        s.bands = c["bands"].get<std::vector<double>>();
        s.desired = c["desired"].get<std::vector<double>>();
        if (!c["weight"].is_null()) s.weight = c["weight"].get<std::vector<double>>();
        s.type = *parse_remez_type(type);
        const auto h = remez(s);
        INFO((h ? std::string{} : h.error().message));
        REQUIRE(h);
        const auto expected = c["h"].get<std::vector<double>>();
        REQUIRE(h->size() == expected.size());
        CHECK(max_abs_error(*h, expected) <= 1e-8);
    }
}

TEST_CASE("firls matches scipy", "[fir][golden]") {
    const auto g = signals::test::load_golden("fir.json");
    for (const auto& c : g["firls"]) {
        INFO("numtaps=" << c["numtaps"].get<int>());
        std::vector<double> weight;
        if (!c["weight"].is_null()) weight = c["weight"].get<std::vector<double>>();
        const auto h = firls(c["numtaps"], c["bands"].get<std::vector<double>>(), c["desired"].get<std::vector<double>>(), weight);
        REQUIRE(h);
        const auto expected = c["h"].get<std::vector<double>>();
        REQUIRE(h->size() == expected.size());
        CHECK(max_abs_error(*h, expected) <= 1e-10);
    }
}

TEST_CASE("fir designs reject impossible specs", "[fir]") {
    RemezSpec s;
    s.numtaps = 31;
    s.bands = {0, 0.2, 0.19, 0.5};
    s.desired = {1, 0};
    CHECK_FALSE(remez(s));
    s.bands = {0, 0.2, 0.3, 0.6};
    CHECK_FALSE(remez(s));
    CHECK_FALSE(firls(30, std::vector<double>{0, 0.3, 0.4, 1}, std::vector<double>{1, 1, 0, 0}, std::vector<double>{}));
    CHECK_FALSE(firls(31, std::vector<double>{0, 0.3, 0.4}, std::vector<double>{1, 1, 0}, std::vector<double>{}));
    RemezSpec narrow;
    narrow.numtaps = 5;
    narrow.bands = {0, 0.2499, 0.25, 0.5};
    narrow.desired = {1, 0};
    narrow.max_iterations = 3;
    const auto r = remez(narrow);
    if (!r) CHECK(r.error().code == Errc::no_convergence);
}

TEST_CASE("firwin matches scipy for every window and band type", "[fir][golden]") {
    const auto g = signals::test::load_golden("fir.json");
    for (const auto& c : g["firwin"]) {
        const auto name = c["window"].get<std::string>();
        INFO(name << " numtaps=" << c["numtaps"].get<int>() << " pass_zero=" << c["pass_zero"].get<bool>());
        const auto window = parse_fir_window(name);
        REQUIRE(window);
        const auto h = firwin(c["numtaps"], c["cutoff"].get<std::vector<double>>(), *window, c["pass_zero"], c["beta"]);
        INFO((h ? std::string{} : h.error().message));
        REQUIRE(h);
        CHECK(max_abs_error(*h, c["h"].get<std::vector<double>>()) <= 1e-12);
    }
}

TEST_CASE("firwin2 matches scipy", "[fir][golden]") {
    const auto g = signals::test::load_golden("fir.json");
    for (const auto& c : g["firwin2"]) {
        INFO(c["window"].get<std::string>() << " numtaps=" << c["numtaps"].get<int>());
        const auto h = firwin2(c["numtaps"], c["freq"].get<std::vector<double>>(), c["gain"].get<std::vector<double>>(), *parse_fir_window(c["window"].get<std::string>()));
        INFO((h ? std::string{} : h.error().message));
        REQUIRE(h);
        CHECK(max_abs_error(*h, c["h"].get<std::vector<double>>()) <= 1e-12);
    }
}

TEST_CASE("firwin refuses what scipy refuses", "[fir]") {
    const std::vector<double> half{0.5};
    CHECK_FALSE(firwin(30, half, FirWindow::hamming, false));
    CHECK_FALSE(firwin(31, std::vector<double>{0.6, 0.4}, FirWindow::hamming, true));
    CHECK_FALSE(firwin(31, std::vector<double>{1.2}, FirWindow::hamming, true));
    CHECK_FALSE(firwin(0, half, FirWindow::hamming, true));
    CHECK_FALSE(firwin2(30, std::vector<double>{0, 1}, std::vector<double>{0, 1}, FirWindow::hamming));
    CHECK_FALSE(firwin2(31, std::vector<double>{0, 0.6, 0.5, 1}, std::vector<double>{1, 1, 0, 0}, FirWindow::hamming));
    CHECK_FALSE(parse_fir_window("triangle"));
}
