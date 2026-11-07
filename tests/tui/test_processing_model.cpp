#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <string>

#include "support/golden.hpp"
#include "support/paths.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/processing_model.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;

TEST_CASE("processing the canvas fragment matches the canvas numbers", "[tui][processing][golden]") {
    const auto g = signals::test::load_golden("processing.json");
    const auto clean = g["clean"].get<std::vector<double>>();
    const auto noisy = g["noisy"].get<std::vector<double>>();
    for (const auto& [key, expected] : g["combos"].items()) {
        INFO(key);
        ProcessingOptions o;
        o.rule = key.starts_with("bayes") ? signals::core::ShrinkRule::bayes : signals::core::ShrinkRule::visu;
        o.threshold = key.find("/hard/") != std::string::npos ? signals::core::ThresholdMode::hard : signals::core::ThresholdMode::soft;
        o.scale = std::stod(key.substr(key.rfind('/') + 1));
        const auto r = process_fragment(clean, noisy, 44100, o);
        REQUIRE(r);
        CHECK(r->sigma == Catch::Approx(g["sigma"].get<double>()).epsilon(1e-9));
        CHECK(r->snr_in == Catch::Approx(g["snr_in"].get<double>()).epsilon(1e-9));
        CHECK(r->snr_out == Catch::Approx(expected["snr_out"].get<double>()).margin(1e-6));
        CHECK(r->kept == expected["kept"].get<std::size_t>());
        CHECK(r->err_db == Catch::Approx(expected["err_db"].get<double>()).margin(1e-6));
        CHECK(r->total == 8265);
    }
    ProcessingOptions o;
    const auto r = process_fragment(clean, noisy, 44100, o);
    REQUIRE(r);
    for (const auto& point : g["curve"]) {
        const auto k = point[0].get<std::size_t>();
        const auto it = std::ranges::find_if(r->curve, [&](const CurvePoint& p) { return p.k == k; });
        REQUIRE(it != r->curve.end());
        const double expected_db = point[1].get<double>();
        if (expected_db < -150) CHECK(it->err_db < -150);
        else CHECK(it->err_db == Catch::Approx(expected_db).margin(1e-6));
    }
}

TEST_CASE("band gains give the canvas transfer and aliasing", "[tui][processing][golden]") {
    const auto g = signals::test::load_golden("processing.json");
    const auto bins = g["bins"].get<std::vector<std::size_t>>();
    for (const auto& [key, expected] : g["transfer"].items()) {
        INFO(key);
        const auto t = band_transfer("sym8", 5, expected["gains"].get<std::vector<double>>(), g["np"].get<std::size_t>(), bins);
        REQUIRE(t);
        REQUIRE(t->t_db.size() == bins.size());
        for (std::size_t i = 0; i < bins.size(); ++i) {
            CHECK(t->t_db[i] == Catch::Approx(expected["t_db"][i].get<double>()).margin(1e-6));
            const double alias = expected["a_db"][i].get<double>();
            if (alias < -150) CHECK(t->a_db[i] < -150);
            else CHECK(t->a_db[i] == Catch::Approx(alias).margin(0.05));
        }
    }
}

TEST_CASE("a silent fragment gives finite numbers", "[tui][processing]") {
    const std::vector<double> zero(8192, 0.0);
    const auto r = process_fragment(zero, zero, 44100, ProcessingOptions{});
    REQUIRE(r);
    CHECK(r->sigma == 0);
    CHECK(std::isfinite(r->err_db));
    CHECK_FALSE(std::isnan(r->snr_out));
    for (const auto& p : r->curve) CHECK(std::isfinite(p.err_db));
}

TEST_CASE("a fragment too short for J is refused with a hint", "[tui][processing]") {
    ProcessingOptions o;
    o.levels = 12;
    const std::vector<double> x(1000, 0.1);
    const auto r = process_fragment(x, x, 44100, o);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == signals::core::Errc::level_out_of_range);
}

TEST_CASE("the model takes a fragment of the open file and adds seeded noise", "[tui][processing]") {
    JobRunner jobs{[] {}, 0ms, true};
    auto signal = std::make_shared<SignalModel>();
    signal->open(signals::test::sample("test.wav"), jobs);
    ProcessingModel model{signal};
    model.sync(jobs);
    const auto r = model.result();
    REQUIRE(r);
    CHECK(r->clean.size() == 8192);
    CHECK(r->snr_in == Catch::Approx(10).margin(0.3));
    CHECK(r->snr_out > r->snr_in);
    auto o = model.options();
    o.rule = signals::core::ShrinkRule::sure;
    model.set_options(o, jobs);
    const auto s = model.result();
    REQUIRE(s);
    CHECK(s->options.rule == signals::core::ShrinkRule::sure);
    CHECK(s->clean == r->clean);
    CHECK(s->noisy == r->noisy);
}
