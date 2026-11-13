#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "core/sst.hpp"
#include "support/golden.hpp"

using namespace signals::core;

namespace {

double worst(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return INFINITY;
    double w = 0;
    for (std::size_t k = 0; k < a.size(); ++k) w = std::max(w, std::abs(a[k] - b[k]));
    return w;
}

double rms_error(const std::vector<double>& a, const std::vector<double>& b, std::size_t from, std::size_t to) {
    double e = 0, r = 0;
    for (std::size_t k = from; k < to; ++k) {
        e += (a[k] - b[k]) * (a[k] - b[k]);
        r += b[k] * b[k];
    }
    return std::sqrt(e / r);
}

std::vector<double> tone(std::size_t n, double f, double amplitude, double phase = 0) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) x[k] = amplitude * std::cos(2 * std::numbers::pi * f * static_cast<double>(k) + phase);
    return x;
}

}  // namespace

TEST_CASE("the synchrosqueezed CWT matches ssqueezepy bin by bin", "[sst][golden]") {
    const auto g = signals::test::load_golden("sst.json");
    for (const auto& c : g["cases"]) {
        const auto n = c["n"].get<std::size_t>();
        const auto count = c["count"].get<std::size_t>();
        INFO(c["kind"].get<std::string>() << ", " << n << " samples, mu " << c["mu"].get<double>() << ", " << c["spacing"].get<std::string>() << " bins");
        const sst::Options o{c["mu"].get<double>(), c["s0"].get<double>(), c["voices"].get<int>(), count};
        const sst::Bins bins{c["freqs"].get<std::vector<double>>(), c["spacing"].get<std::string>() == "log" ? sst::Spacing::log : sst::Spacing::linear};
        const auto x = c["x"].get<std::vector<double>>();
        const auto tx = sst::transform(x, o, bins);
        REQUIRE(tx);
        REQUIRE(tx->size() == count);
        std::vector<double> re, im;
        for (const auto& row : *tx) {
            REQUIRE(row.size() == n);
            for (const auto& z : row) re.push_back(z.real()), im.push_back(z.imag());
        }
        CHECK(worst(re, c["tx_re"].get<std::vector<double>>()) < 1e-10);
        CHECK(worst(im, c["tx_im"].get<std::vector<double>>()) < 1e-10);
        if (c.contains("wx_re")) {
            std::vector<double> wre(count * n), wim(count * n);
            REQUIRE(sst::cwt_rows(x, o, [&](std::size_t a, std::span<const std::complex<double>> wx, std::span<const std::complex<double>>) {
                for (std::size_t t = 0; t < n; ++t) wre[a * n + t] = wx[t].real(), wim[a * n + t] = wx[t].imag();
            }));
            CHECK(worst(wre, c["wx_re"].get<std::vector<double>>()) < 1e-12);
            CHECK(worst(wim, c["wx_im"].get<std::vector<double>>()) < 1e-12);
        }
        const double css = sst::admissibility(o.mu);
        CHECK(std::abs(css - c["css_exact"].get<double>()) < 1e-13 * css);
        CHECK(std::abs(css - c["css"].get<double>()) < 2e-7 * css);
        auto y = sst::inverse(*tx, o.mu);
        for (auto& v : y) v *= css / c["css"].get<double>();
        CHECK(worst(y, c["inverse"].get<std::vector<double>>()) < 1e-10);
        const auto whole = sst::band(x, o, 0, std::numeric_limits<double>::infinity());
        REQUIRE(whole);
        CHECK(worst(*whole, sst::inverse(*tx, o.mu)) < 1e-12);
    }
}

TEST_CASE("an SST band recovers one of two tones and the whole band recovers the signal", "[sst]") {
    constexpr std::size_t n = 4096, edge = 512;
    const auto low = tone(n, 0.05, 1.0), high = tone(n, 0.2, 0.5, 0.3);
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) x[k] = low[k] + high[k];
    const auto o = sst::covering(0.01, 0.48, 32, 6.0);
    CHECK(o.count == static_cast<std::size_t>(std::floor(32 * std::log2(0.48 / 0.01) + 1e-9)) + 1);
    const auto a = sst::band(x, o, 0.03, 0.08), b = sst::band(x, o, 0.15, 0.25), all = sst::band(x, o, 0, 0.5);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(all);
    CHECK(rms_error(*a, low, edge, n - edge) < 0.02);
    CHECK(rms_error(*b, high, edge, n - edge) < 0.02);
    CHECK(rms_error(*all, x, edge, n - edge) < 0.02);
}

TEST_CASE("the SST of a tone concentrates in the bin of its frequency", "[sst]") {
    constexpr std::size_t n = 2048;
    const auto x = tone(n, 0.1, 1.0);
    const auto o = sst::covering(0.02, 0.45, 16, 6.0);
    const auto bins = sst::log_bins(0.02, 0.45, 60);
    REQUIRE(bins.freqs.size() == 60);
    CHECK(std::abs(bins.freqs.front() - 0.02) < 1e-15);
    CHECK(std::abs(bins.freqs.back() - 0.45) < 1e-15);
    const auto map = sst::power_map(x, o, bins, 8);
    REQUIRE(map);
    REQUIRE(map->rows == 60);
    REQUIRE(map->cols == 8);
    const auto nearest = static_cast<std::size_t>(std::distance(
        bins.freqs.begin(), std::min_element(bins.freqs.begin(), bins.freqs.end(), [](double p, double q) { return std::abs(p - 0.1) < std::abs(q - 0.1); })));
    for (std::size_t c = 2; c < 6; ++c) {
        double total = 0, peak = 0;
        std::size_t best = 0;
        for (std::size_t r = 0; r < map->rows; ++r) {
            const double p = map->power[r * map->cols + c];
            total += p;
            if (p > peak) peak = p, best = r;
        }
        CHECK(best == nearest);
        CHECK(peak > 0.8 * total);
    }
}

TEST_CASE("a segmented SST power map shows the same picture as the whole transform", "[sst]") {
    constexpr std::size_t n = 20000;
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k);
        x[k] = std::cos(2 * std::numbers::pi * (0.02 * t + 0.4 * t * t / (2 * n))) + 0.3 * std::sin(0.9 * t);
    }
    const auto o = sst::covering(0.01, 0.45, 16, 6.0);
    const auto bins = sst::log_bins(0.01, 0.45, 40);
    const auto whole = sst::power_map(x, o, bins, 50, nullptr, n);
    const auto parts = sst::power_map(x, o, bins, 50, nullptr, 3000);
    REQUIRE(whole);
    REQUIRE(parts);
    REQUIRE(whole->power.size() == parts->power.size());
    const double peak = *std::max_element(whole->power.begin(), whole->power.end());
    std::size_t strong = 0;
    for (std::size_t i = 0; i < whole->power.size(); ++i) {
        if (whole->power[i] > 1e-4 * peak) {
            ++strong;
            CHECK(std::abs(10 * std::log10(parts->power[i] / whole->power[i])) < 0.1);
        } else {
            CHECK(parts->power[i] < 2e-4 * peak);
        }
    }
    CHECK(strong > 100);
}

TEST_CASE("the SST refuses empty signals, bad scales and bad bins", "[sst]") {
    const std::vector<double> x(64, 1.0);
    const sst::Options good{6.0, 2.0, 8, 10};
    const auto bins = sst::log_bins(0.02, 0.4, 10);
    CHECK_FALSE(sst::transform(std::vector<double>{}, good, bins));
    CHECK_FALSE(sst::transform(x, sst::Options{6.0, 2.0, 0, 10}, bins));
    CHECK_FALSE(sst::transform(x, sst::Options{6.0, 2.0, 8, 0}, bins));
    CHECK_FALSE(sst::transform(x, sst::Options{6.0, -1.0, 8, 10}, bins));
    CHECK_FALSE(sst::transform(x, sst::Options{0.0, 2.0, 8, 10}, bins));
    CHECK_FALSE(sst::transform(x, good, sst::Bins{{0.1}, sst::Spacing::log}));
    CHECK_FALSE(sst::transform(x, good, sst::Bins{{0.2, 0.1, 0.3}, sst::Spacing::linear}));
    CHECK_FALSE(sst::transform(x, good, sst::Bins{{0.0, 0.1, 0.2}, sst::Spacing::log}));
    auto broken = x;
    broken[3] = std::numeric_limits<double>::quiet_NaN();
    CHECK_FALSE(sst::band(broken, good, 0.0, 0.5));
    CHECK_FALSE(sst::band(x, good, 0.3, 0.1));
    CHECK_FALSE(sst::power_map(x, good, bins, 0));
}

TEST_CASE("the SST keeps a one- and two-sample signal finite", "[sst]") {
    for (const auto& x : {std::vector<double>{2.0}, std::vector<double>{1.0, -1.0}}) {
        const auto tx = sst::transform(x, sst::Options{6.0, 1.0, 2, 4}, sst::log_bins(0.05, 0.45, 4));
        REQUIRE(tx);
        for (const auto& row : *tx)
            for (const auto& z : row) CHECK(std::isfinite(z.real()));
    }
}

TEST_CASE("the SST refuses samples large enough to overflow and keeps large ones finite", "[sst]") {
    const sst::Options o{6.0, 2.0, 8, 20};
    const auto bins = sst::log_bins(0.02, 0.45, 20);
    std::vector<double> x(256);
    for (std::size_t k = 0; k < x.size(); ++k) x[k] = 1e149 * std::cos(0.7 * static_cast<double>(k));
    const auto tx = sst::transform(x, o, bins);
    REQUIRE(tx);
    for (const auto& row : *tx)
        for (const auto& z : row) REQUIRE(std::isfinite(std::abs(z)));
    x[10] = 1e300;
    CHECK_FALSE(sst::transform(x, o, bins));
    CHECK_FALSE(sst::band(x, o, 0.0, 0.5));
    CHECK_FALSE(sst::power_map(x, o, bins, 4));
}

TEST_CASE("the SST ridge follows a chirp and reports its amplitude", "[sst]") {
    constexpr std::size_t n = 4096, edge = 400;
    constexpr double f0 = 0.05, f1 = 0.2, a = 0.7;
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double t = static_cast<double>(k);
        x[k] = a * std::cos(2 * std::numbers::pi * (f0 * t + (f1 - f0) * t * t / (2 * n)));
    }
    const auto o = sst::covering(0.02, 0.4, 32, 6.0);
    const auto r = sst::ridge(x, o, 0.02, 0.4);
    REQUIRE(r);
    REQUIRE(r->freq.size() == n);
    REQUIRE(r->amp.size() == n);
    for (std::size_t k = edge; k < n - edge; k += 37) {
        const double expected = f0 + (f1 - f0) * static_cast<double>(k) / n;
        CHECK(std::abs(r->freq[k] - expected) < 0.01 * expected);
        CHECK(std::abs(r->amp[k] - a) < 0.02 * a);
    }
}

TEST_CASE("the SST ridge picks the strongest tone and a band picks the weaker", "[sst]") {
    constexpr std::size_t n = 4096;
    const auto low = tone(n, 0.05, 1.0), high = tone(n, 0.2, 0.3, 1.0);
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) x[k] = low[k] + high[k];
    const auto o = sst::covering(0.02, 0.45, 32, 6.0);
    const auto all = sst::ridge(x, o, 0.0, 0.5), upper = sst::ridge(x, o, 0.12, 0.3);
    REQUIRE(all);
    REQUIRE(upper);
    for (std::size_t k = 500; k < n - 500; k += 101) {
        CHECK(std::abs(all->freq[k] - 0.05) < 5e-4);
        CHECK(std::abs(all->amp[k] - 1.0) < 0.02);
        CHECK(std::abs(upper->freq[k] - 0.2) < 2e-3);
        CHECK(std::abs(upper->amp[k] - 0.3) < 0.02);
    }
    const auto silent = sst::ridge(std::vector<double>(256, 0.0), o, 0.0, 0.5);
    REQUIRE(silent);
    CHECK(std::ranges::all_of(silent->freq, [](double f) { return f == 0; }));
    CHECK(std::ranges::all_of(silent->amp, [](double v) { return v == 0; }));
    CHECK_FALSE(sst::ridge(x, o, 0.3, 0.1));
}
