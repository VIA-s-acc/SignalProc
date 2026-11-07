#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <memory>

#include <nlohmann/json.hpp>

#include "support/paths.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/spectrum_model.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;

namespace {

nlohmann::json canvas_spectrum() {
    std::ifstream in{std::filesystem::path{SIGNALS_SOURCE_DIR} / "docs" / "design" / "spectrum.json"};
    return nlohmann::json::parse(in);
}

struct SpectrumFixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::shared_ptr<SpectrumModel> model;
    SpectrumFixture() {
        signal->open(signals::test::sample("test.wav"), jobs);
        model = std::make_shared<SpectrumModel>(signal);
    }
};

}  // namespace

TEST_CASE("the spectrogram grid matches the canvas data", "[tui][spectrum]") {
    const auto canvas = canvas_spectrum();
    for (const char* preset : {"wide", "narrow"}) {
        INFO(preset);
        SpectrumFixture f;
        SpectrumOptions o;
        o.n = canvas[preset]["n"].get<std::size_t>();
        o.channel = 0;
        f.model->set_options(o, f.jobs);
        f.model->sync(f.jobs);
        const auto s = f.model->spectrogram();
        REQUIRE(s);
        CHECK(s->options.n == o.n);
        GridSpec g;
        g.rows = canvas["meta"]["fbins"].get<std::size_t>();
        g.cols = canvas["meta"]["cols"].get<std::size_t>();
        g.fmin = canvas["meta"]["fmin"].get<double>();
        g.floor_db = canvas["meta"]["floor"].get<double>();
        g.t1 = s->duration;
        const auto grid = spectrogram_grid(*s, g);
        const auto expected = canvas[preset]["grid"].get<std::vector<std::vector<int>>>();
        REQUIRE(grid.size() == g.rows * g.cols);
        int worse = 0;
        for (std::size_t r = 0; r < g.rows; ++r)
            for (std::size_t c = 0; c < g.cols; ++c)
                if (std::abs(grid[r * g.cols + c] - expected[r][c]) > 1.0) ++worse;
        CHECK(worse == 0);
    }
}

TEST_CASE("the selection spectrum finds the canvas peaks, f0 and harmonics", "[tui][spectrum]") {
    const auto canvas = canvas_spectrum();
    SpectrumFixture f;
    SpectrumOptions o;
    o.channel = 0;
    f.model->set_options(o, f.jobs);
    f.model->set_selection(2.10, 4.85, f.jobs);
    f.model->sync(f.jobs);
    const auto sel = f.model->selection();
    REQUIRE(sel);
    CHECK(sel->t0 == 2.10);
    CHECK(sel->t1 == 4.85);
    const auto& peaks = canvas["peaks"];
    REQUIRE(sel->peaks.size() == peaks.size());
    for (std::size_t i = 0; i < peaks.size(); ++i) {
        CHECK(sel->f[sel->peaks[i].index] == Catch::Approx(peaks[i]["f"].get<double>()).margin(0.06));
        CHECK(sel->db[sel->peaks[i].index] == Catch::Approx(peaks[i]["db"].get<double>()).margin(0.06));
    }
    REQUIRE(sel->f0);
    CHECK(*sel->f0 == Catch::Approx(canvas["f0"].get<double>()).margin(0.06));
    const auto& harm = canvas["harmonics"];
    REQUIRE(sel->harmonics.size() == harm.size());
    for (std::size_t i = 0; i < harm.size(); ++i) CHECK(sel->harmonics[i].f == Catch::Approx(harm[i]["f"].get<double>()).margin(0.06));
}

TEST_CASE("box level averages the power inside the rectangle", "[tui][spectrum]") {
    SpectrumFixture f;
    f.model->sync(f.jobs);
    const auto s = f.model->spectrogram();
    REQUIRE(s);
    const double inside = box_level_db(*s, 5.20, 6.10, 400, 1200);
    CHECK(std::isfinite(inside));
    CHECK(inside <= 0);
    CHECK(box_level_db(*s, 6.10, 5.20, 1200, 400) == inside);
    CHECK(box_level_db(*s, 5.20, 6.10, 400, 1200) > box_level_db(*s, 5.20, 6.10, 18000, 20000));
}

TEST_CASE("a new window size hides the old spectrogram until it is ready", "[tui][spectrum]") {
    SpectrumFixture f;
    f.model->sync(f.jobs);
    const auto first = f.model->spectrogram();
    REQUIRE(first);
    SpectrumOptions o = f.model->options();
    o.n = 512;
    f.model->set_options(o, f.jobs);
    const auto second = f.model->spectrogram();
    REQUIRE(second);
    CHECK(second->options.n == 512);
    CHECK(second != first);
    f.model->set_options(o, f.jobs);
    CHECK(f.model->spectrogram() == second);
}

TEST_CASE("a file shorter than the window is an error, not a crash", "[tui][spectrum]") {
    SpectrumFixture f;
    SpectrumOptions o;
    o.n = std::size_t{1} << 24;
    f.model->set_options(o, f.jobs);
    f.model->sync(f.jobs);
    CHECK_FALSE(f.model->spectrogram());
    REQUIRE(f.model->error());
}
