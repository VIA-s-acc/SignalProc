#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "tui/screens/resample_model.hpp"

using namespace signals::tui;

namespace {

double peak_near(const ResampleView& v, double f, double width) {
    double best = -1e300;
    for (std::size_t k = 0; k < v.f_out.size(); ++k)
        if (std::abs(v.f_out[k] - f) <= width) best = std::max(best, v.psd_out_db[k]);
    return best;
}

double peak_frequency(const ResampleView& v) {
    const auto it = std::max_element(v.psd_out_db.begin(), v.psd_out_db.end());
    return v.f_out[static_cast<std::size_t>(it - v.psd_out_db.begin())];
}

}  // namespace

TEST_CASE("folded frequency follows the folding formula", "[tui][resample]") {
    CHECK(folded_frequency(7000, 12000) == 5000);
    CHECK(folded_frequency(6000, 12000) == 6000);
    CHECK(folded_frequency(13000, 12000) == 1000);
    CHECK(folded_frequency(0, 12000) == 0);
    CHECK(folded_frequency(-3000, 12000) == 3000);
    CHECK(folded_frequency(24000, 48000) == 24000);
    CHECK(folded_frequency(5000, 0) == 5000);
}

TEST_CASE("decimating a 7 kHz tone by 4 folds it to 5 kHz unless the filter removes it", "[tui][resample]") {
    ResampleOptions o;
    o.mode = ResampleMode::down;
    o.m = 4;
    o.source = ResampleSource::tone;
    o.tone_hz = 7000;
    o.filter = AntiAlias::bypass;
    const auto x = resample_source(o, 48000, 8192);
    auto v = compute_resample(o, x, 48000);
    REQUIRE(v);
    CHECK(v->fs_out == 12000);
    REQUIRE(v->tone_out);
    CHECK(*v->tone_out == 5000);
    CHECK(std::abs(peak_frequency(*v) - 5000) < 30);
    REQUIRE(v->copies_db.size() == 4);
    for (const auto& c : v->copies_db) CHECK(c.size() == v->f_out.size());
    o.filter = AntiAlias::on;
    v = compute_resample(o, x, 48000);
    REQUIRE(v);
    CHECK(peak_near(*v, 5000, 50) < -70);
    CHECK(v->aa_attenuation_db > 70);
    CHECK(v->copies_db.empty());
}

TEST_CASE("upsampling a tone by 3 leaves images unless the filter removes them", "[tui][resample]") {
    ResampleOptions o;
    o.mode = ResampleMode::up;
    o.l = 3;
    o.source = ResampleSource::tone;
    o.tone_hz = 1000;
    o.filter = AntiAlias::bypass;
    const auto x = resample_source(o, 48000, 8192);
    auto v = compute_resample(o, x, 48000);
    REQUIRE(v);
    CHECK(v->fs_out == 144000);
    CHECK(peak_near(*v, 47000, 200) > -10);
    CHECK(peak_near(*v, 49000, 200) > -10);
    REQUIRE(v->y_head.size() >= 9);
    CHECK(v->y_head[1] == 0);
    CHECK(v->y_head[2] == 0);
    o.filter = AntiAlias::on;
    v = compute_resample(o, x, 48000);
    REQUIRE(v);
    CHECK(peak_near(*v, 47000, 200) < -70);
    CHECK(peak_near(*v, 1000, 100) > -10);
}

TEST_CASE("rational resampling reports its output rate, also for M = L", "[tui][resample]") {
    ResampleOptions o;
    o.mode = ResampleMode::rational;
    o.l = 3;
    o.m = 2;
    o.source = ResampleSource::sweep;
    const auto x = resample_source(o, 48000, 8192);
    auto v = compute_resample(o, x, 48000);
    REQUIRE(v);
    CHECK(v->fs_out == 72000);
    o.m = 3;
    v = compute_resample(o, x, 48000);
    REQUIRE(v);
    CHECK(v->fs_out == 48000);
    CHECK_FALSE(compute_resample(o, std::vector<double>{}, 48000));
    CHECK_FALSE(compute_resample(o, std::vector<double>(10, 1.0), 48000));
}

TEST_CASE("m7c: the aliasing copies of white noise add up to the decimated spectrum", "[tui][resample]") {
    ResampleOptions o;
    o.source = ResampleSource::file;
    o.mode = ResampleMode::down;
    o.m = 4;
    o.filter = AntiAlias::bypass;
    std::vector<double> x(8192);
    std::uint32_t state = 7;
    for (auto& v : x) {
        state = state * 1664525u + 1013904223u;
        v = static_cast<double>(state >> 8) / 16777216.0 - 0.5;
    }
    const auto v = compute_resample(o, x, 48000);
    REQUIRE(v);
    REQUIRE(v->copies_db.size() == 4);
    std::vector<double> gap;
    for (std::size_t b = v->f_out.size() / 8; b < v->f_out.size() * 7 / 8; ++b) {
        double sum = 0;
        for (const auto& c : v->copies_db) sum += std::pow(10.0, c[b] / 10);
        gap.push_back(10 * std::log10(sum) - v->psd_out_db[b]);
    }
    std::nth_element(gap.begin(), gap.begin() + static_cast<std::ptrdiff_t>(gap.size() / 2), gap.end());
    CHECK(std::abs(gap[gap.size() / 2]) < 1.5);
}

TEST_CASE("m7c: the filter is drawn on the panel where its cut lies", "[tui][resample]") {
    ResampleView v;
    v.down = 4;
    CHECK(filter_panel(v) == FilterPanel::before);
    v.up = 2;
    v.down = 3;
    CHECK(filter_panel(v) == FilterPanel::before);
    v.up = 3;
    v.down = 2;
    CHECK(filter_panel(v) == FilterPanel::after);
    v.up = 3;
    v.down = 1;
    CHECK(filter_panel(v) == FilterPanel::after);
    v.up = v.down = 1;
    CHECK(filter_panel(v) == FilterPanel::none);
}

TEST_CASE("m7c review: a tone's aliasing copy reaches the output peak it explains", "[tui][resample]") {
    for (const std::size_t m : {2u, 4u, 8u}) {
        INFO("M = " << m);
        ResampleOptions o;
        o.mode = ResampleMode::down;
        o.m = m;
        o.source = ResampleSource::tone;
        o.tone_hz = 7000;
        o.filter = AntiAlias::bypass;
        const auto x = resample_source(o, 48000, 8192);
        const auto v = compute_resample(o, x, 48000);
        REQUIRE(v);
        REQUIRE(v->copies_db.size() == m);
        double copy_peak = -1e300;
        for (const auto& c : v->copies_db) copy_peak = std::max(copy_peak, *std::max_element(c.begin(), c.end()));
        const double out_peak = *std::max_element(v->psd_out_db.begin(), v->psd_out_db.end());
        CHECK(std::abs(copy_peak - out_peak) < 1.0);
    }
}
