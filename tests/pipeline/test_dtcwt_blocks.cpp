#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"

using namespace signals;
using namespace signals::pipeline;

namespace {

core::Result<std::vector<Value>> run_block(const std::string& id, std::vector<Value> in, Params p = {}) {
    const auto* type = find_block_type(id);
    REQUIRE(type);
    auto params = resolve_params(*type, p);
    REQUIRE(params);
    RunContext ctx{std::filesystem::temp_directory_path()};
    return type->run(ctx, in, *params);
}

core::AudioBuffer tone(std::size_t n, std::size_t shift) {
    core::AudioBuffer a;
    a.sample_rate = 8000;
    for (int c = 0; c < 2; ++c) {
        std::vector<double> x(n);
        for (std::size_t k = 0; k < n; ++k) {
            const double t = static_cast<double>(k + shift);
            x[k] = std::sin(0.21 * t + c) * std::exp(-std::pow((t - 300.0) / 60.0, 2)) + 0.3 * std::cos(1.3 * t);
        }
        a.channels.push_back(std::move(x));
    }
    return a;
}

std::vector<double> level_energies(const Coeffs& c, bool complex) {
    std::vector<double> out;
    for (std::size_t b = 1; b < c.channels[0].bands.size(); ++b) {
        double e = 0;
        const auto& band = c.channels[0].bands[b];
        if (complex)
            for (std::size_t k = 0; k + 1 < band.size(); k += 2) e += band[k] * band[k] + band[k + 1] * band[k + 1];
        else
            for (double v : band) e += v * v;
        out.push_back(e);
    }
    return out;
}

double worst_change(const std::vector<double>& a, const std::vector<double>& b) {
    double total = 0, w = 0;
    for (double v : a) total += v;
    for (std::size_t k = 0; k < a.size(); ++k) w = std::max(w, std::abs(a[k] - b[k]) / total);
    return w;
}

}  // namespace

TEST_CASE("dtcwt and idtcwt give back a stereo signal of any length to 1e-12", "[pipeline][dtcwt]") {
    for (const std::size_t n : {1000, 999, 2}) {
        INFO(n << " samples");
        const auto x = tone(n, 0);
        auto coeffs = run_block("dtcwt", {x}, {{"levels", n > 2 ? "5" : "1"}, {"biort", "near_sym_b"}, {"qshift", "qshift_c"}});
        REQUIRE(coeffs);
        const auto& c = std::get<Coeffs>(coeffs->front());
        CHECK(c.transform == "dtcwt");
        CHECK(c.channels.size() == 2);
        CHECK(c.levels() == (n > 2 ? 5u : 1u));
        auto back = run_block("idtcwt", {c});
        REQUIRE(back);
        const auto& y = std::get<core::AudioBuffer>(back->front());
        REQUIRE(y.channels.size() == 2);
        for (std::size_t ch = 0; ch < 2; ++ch) {
            REQUIRE(y.channels[ch].size() == n);
            double w = 0;
            for (std::size_t k = 0; k < n; ++k) w = std::max(w, std::abs(y.channels[ch][k] - x.channels[ch][k]));
            CHECK(w < 1e-12);
        }
        CHECK(y.sample_rate == 8000);
    }
}

TEST_CASE("a one-sample shift barely moves DTCWT level energies but does move DWT ones", "[pipeline][dtcwt]") {
    const auto a = run_block("dtcwt", {tone(512, 0)}, {{"levels", "4"}});
    const auto b = run_block("dtcwt", {tone(512, 1)}, {{"levels", "4"}});
    const auto da = run_block("dwt", {tone(512, 0)}, {{"wavelet", "db4"}, {"levels", "4"}});
    const auto db = run_block("dwt", {tone(512, 1)}, {{"wavelet", "db4"}, {"levels", "4"}});
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(da);
    REQUIRE(db);
    const double complex_change = worst_change(level_energies(std::get<Coeffs>(a->front()), true), level_energies(std::get<Coeffs>(b->front()), true));
    const double real_change = worst_change(level_energies(std::get<Coeffs>(da->front()), false), level_energies(std::get<Coeffs>(db->front()), false));
    INFO("dtcwt " << complex_change << ", dwt " << real_change);
    CHECK(complex_change < 0.02);
    CHECK(real_change > 3 * complex_change);
}

TEST_CASE("idtcwt refuses other coefficients and idwt points DTCWT coefficients to idtcwt", "[pipeline][dtcwt]") {
    const auto dwt = run_block("dwt", {tone(64, 0)}, {{"wavelet", "haar"}, {"levels", "2"}});
    REQUIRE(dwt);
    const auto wrong = run_block("idtcwt", {dwt->front()});
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().message.find("idwt") != std::string::npos);
    const auto c = run_block("dtcwt", {tone(64, 0)}, {{"levels", "2"}});
    REQUIRE(c);
    const auto back = run_block("idwt", {c->front()});
    REQUIRE_FALSE(back);
    CHECK(back.error().message.find("idtcwt") != std::string::npos);
}
