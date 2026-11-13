#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

#include "pipeline/block.hpp"

using namespace signals;
using namespace signals::pipeline;

namespace {

constexpr double kRate = 8000;

core::Result<std::vector<Value>> run_block(const std::string& id, std::vector<Value> in, Params p = {}) {
    const auto* type = find_block_type(id);
    REQUIRE(type);
    auto params = resolve_params(*type, p);
    REQUIRE(params);
    RunContext ctx{std::filesystem::temp_directory_path()};
    return type->run(ctx, in, *params);
}

std::vector<double> sine(std::size_t n, double f, double a, double phase = 0) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) x[k] = a * std::cos(2 * std::numbers::pi * f * static_cast<double>(k) / kRate + phase);
    return x;
}

double rms_error(const std::vector<double>& a, const std::vector<double>& b, std::size_t edge) {
    double e = 0, r = 0;
    for (std::size_t k = edge; k + edge < a.size(); ++k) e += (a[k] - b[k]) * (a[k] - b[k]), r += b[k] * b[k];
    return std::sqrt(e / r);
}

double field(const Report& r, const std::string& name) {
    for (const auto& [key, value] : r.fields)
        if (key == name) return std::get<double>(value);
    FAIL("no report field " << name);
    return 0;
}

}  // namespace

TEST_CASE("sst_band pulls one tone out of each stereo channel", "[pipeline][sst]") {
    constexpr std::size_t n = 4096;
    const auto low0 = sine(n, 400, 1.0), high0 = sine(n, 1600, 0.5, 0.4), low1 = sine(n, 400, 0.3, 1.0), high1 = sine(n, 1600, 0.8);
    core::AudioBuffer in;
    in.sample_rate = kRate;
    in.channels = {std::vector<double>(n), std::vector<double>(n)};
    for (std::size_t k = 0; k < n; ++k) in.channels[0][k] = low0[k] + high0[k], in.channels[1][k] = low1[k] + high1[k];
    const auto low = run_block("sst_band", {in}, {{"lo", "300"}, {"hi", "600"}});
    const auto high = run_block("sst_band", {in}, {{"lo", "1200"}, {"hi", "2000"}});
    REQUIRE(low);
    REQUIRE(high);
    const auto& a = std::get<core::AudioBuffer>(low->front());
    const auto& b = std::get<core::AudioBuffer>(high->front());
    REQUIRE(a.channels.size() == 2);
    CHECK(a.sample_rate == kRate);
    CHECK(a.channels[0].size() == n);
    CHECK(rms_error(a.channels[0], low0, 400) < 0.03);
    CHECK(rms_error(a.channels[1], low1, 400) < 0.03);
    CHECK(rms_error(b.channels[0], high0, 400) < 0.03);
    CHECK(rms_error(b.channels[1], high1, 400) < 0.03);
}

TEST_CASE("sst_ridge tracks a chirp in hertz and reports its amplitude", "[pipeline][sst]") {
    constexpr std::size_t n = 8000;
    constexpr double f0 = 200, f1 = 1200, a = 0.6;
    core::AudioBuffer in;
    in.sample_rate = kRate;
    in.channels.emplace_back(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) / kRate;
        in.channels[0][k] = a * std::sin(2 * std::numbers::pi * (f0 * t + (f1 - f0) * t * t / 2));
    }
    const auto out = run_block("sst_ridge", {in}, {{"lo", "100"}, {"hi", "2000"}});
    REQUIRE(out);
    REQUIRE(out->size() == 3);
    const auto& report = std::get<Report>((*out)[0]);
    const auto& freq = std::get<core::AudioBuffer>((*out)[1]);
    const auto& amp = std::get<core::AudioBuffer>((*out)[2]);
    CHECK(freq.sample_rate == kRate);
    REQUIRE(freq.channels.front().size() == n);
    REQUIRE(amp.channels.front().size() == n);
    for (std::size_t k = 600; k + 600 < n; k += 97) {
        const double expected = f0 + (f1 - f0) * static_cast<double>(k) / kRate;
        CHECK(std::abs(freq.channels[0][k] - expected) < 0.01 * expected);
        CHECK(std::abs(amp.channels[0][k] - a) < 0.02 * a);
    }
    CHECK(std::abs(field(report, "freq_median_hz") - 700) < 20);
    CHECK(field(report, "freq_min_hz") < 260);
    CHECK(field(report, "freq_max_hz") > 1100);
    CHECK(field(report, "found_fraction") > 0.95);
    CHECK(std::abs(field(report, "amp_peak") - a) < 0.1 * a);
}

TEST_CASE("sst_band and sst_ridge refuse a bad band, a missing rate and broken samples", "[pipeline][sst]") {
    core::AudioBuffer in;
    in.sample_rate = kRate;
    in.channels.push_back(sine(512, 500, 1.0));
    for (const std::string id : {"sst_band", "sst_ridge"}) {
        INFO(id);
        CHECK_FALSE(run_block(id, {in}, {{"lo", "600"}, {"hi", "500"}}));
        CHECK_FALSE(run_block(id, {in}, {{"lo", "100"}, {"hi", "4500"}}));
        auto silent = in;
        silent.sample_rate = 0;
        CHECK_FALSE(run_block(id, {silent}, {{"lo", "100"}, {"hi", "1000"}}));
        auto broken = in;
        broken.channels[0][7] = NAN;
        CHECK_FALSE(run_block(id, {broken}, {{"lo", "100"}, {"hi", "1000"}}));
        auto empty = in;
        empty.channels[0].clear();
        CHECK_FALSE(run_block(id, {empty}, {{"lo", "100"}, {"hi", "1000"}}));
    }
}
