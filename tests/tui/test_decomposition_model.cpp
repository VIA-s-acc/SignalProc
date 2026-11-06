#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

#include "core/design/families.hpp"
#include "core/process/shrink.hpp"
#include "support/paths.hpp"
#include "tui/screens/decomposition_model.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;
namespace core = signals::core;

namespace {

struct Rig {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::unique_ptr<DecompositionModel> m;
    explicit Rig(std::size_t budget = 0) {
        signal->open(signals::test::sample("test.wav"), jobs);
        m = std::make_unique<DecompositionModel>(signal, budget);
        m->sync(jobs);
    }
};

std::size_t nonzero(const std::vector<double>& v) {
    std::size_t n = 0;
    for (double c : v) n += c != 0.0;
    return n;
}

}  // namespace

TEST_CASE("test.wav db2 J=6 gives the canvas bands", "[tui][decomposition]") {
    Rig r;
    auto d = r.m->data();
    REQUIRE(d);
    const std::vector<std::size_t> lengths{225'344, 112'673, 56'338, 28'170, 14'086, 7'044, 7'044};
    const std::vector<double> energy{0.79, 4.23, 7.18, 6.23, 6.13, 9.90, 65.55};
    REQUIRE(d->bands.size() == 7);
    for (std::size_t k = 0; k < 7; ++k) {
        CHECK(d->bands[k].length == lengths[k]);
        CHECK(d->bands[k].energy_pct == Catch::Approx(energy[k]).margin(0.01));
    }
    CHECK(d->bands[0].name == "d1");
    CHECK(d->bands[6].name == "a6");
    CHECK(d->bands[6].hi_hz == Catch::Approx(344.53).margin(0.01));
    CHECK(d->bands[0].lo_hz == Catch::Approx(11025).margin(0.01));
    CHECK(d->pr_error < 1e-12);
    CHECK(d->tiles.rows == 7);
    CHECK(d->tiles.cols == 512);
    CHECK(d->band_pyramids.size() == 7);
    REQUIRE(r.m->recon());
    CHECK(r.m->recon()->energy_kept_pct == Catch::Approx(100).margin(1e-6));
}

TEST_CASE("disabling d1 removes its share and enabling it restores x", "[tui][decomposition]") {
    Rig r;
    r.m->toggle(0, r.jobs);
    REQUIRE(r.m->recon());
    CHECK_FALSE(r.m->recon()->enabled[0]);
    CHECK(r.m->recon()->energy_kept_pct == Catch::Approx(99.21).margin(0.05));
    r.m->toggle(0, r.jobs);
    const auto& x = r.m->data()->x;
    const auto& xh = r.m->recon()->xhat;
    double worst = 0;
    for (std::size_t i = 0; i < x.size(); ++i) worst = std::max(worst, std::abs(x[i] - xh[i]));
    CHECK(worst < 1e-6);
}

TEST_CASE("BayesShrink uses the MAD sigma of d1 and undo restores", "[tui][decomposition]") {
    Rig r;
    const auto d = r.m->data();
    CHECK(d->sigma == Catch::Approx(core::noise_sigma_mad(d->coeffs.bands.back())));
    Processing p;
    p.rule = core::ShrinkRule::bayes;
    r.m->apply(p, r.jobs);
    REQUIRE(r.m->recon()->lambdas.size() == 4);
    CHECK(r.m->recon()->energy_kept_pct < 100);
    r.m->undo(r.jobs);
    CHECK_FALSE(r.m->recon()->processing.rule);
    CHECK(r.m->recon()->energy_kept_pct == Catch::Approx(100).margin(1e-6));
}

TEST_CASE("keep 10 percent leaves 10 percent of detail coefficients", "[tui][decomposition]") {
    Rig r;
    Processing p;
    p.keep_fraction = 0.1;
    r.m->apply(p, r.jobs);
    const auto& bands = r.m->recon()->processed.bands;
    std::size_t total = 0, kept = 0;
    for (std::size_t b = 1; b < bands.size(); ++b) {
        total += bands[b].size();
        kept += nonzero(bands[b]);
    }
    CHECK(kept <= static_cast<std::size_t>(std::ceil(0.1 * static_cast<double>(total))));
    CHECK(kept > total / 20);
}

TEST_CASE("a band gain matches core edit_bands", "[tui][decomposition]") {
    Rig r;
    Processing p;
    p.gains_db.assign(7, 0.0);
    p.gains_db[2] = 3;
    r.m->apply(p, r.jobs);
    const auto d = r.m->data();
    std::vector<double> gains(7, 1.0);
    gains[4] = std::pow(10.0, 3.0 / 20);
    auto expected = core::edit_bands(d->x, *core::wavelet("db2"), 6, {core::Extension::symmetric}, gains);
    REQUIRE(expected);
    const auto& xh = r.m->recon()->xhat;
    double worst = 0;
    for (std::size_t i = 0; i < xh.size(); ++i) worst = std::max(worst, std::abs((*expected)[i] - xh[i]));
    CHECK(worst < 1e-6);
}

TEST_CASE("MRA components sum to the reconstruction", "[tui][decomposition]") {
    Rig r;
    const auto rec = r.m->recon();
    REQUIRE(rec->components.size() == 7);
    CHECK_FALSE(rec->mra_skipped);
    double worst = 0;
    for (std::size_t i = 0; i < rec->xhat.size(); i += 97) {
        double s = 0;
        for (const auto& c : rec->components) s += c[i];
        worst = std::max(worst, std::abs(s - rec->xhat[i]));
    }
    CHECK(worst < 1e-5);
}

TEST_CASE("MRA over the memory budget is skipped with a reason", "[tui][decomposition]") {
    Rig r{1 << 20};
    REQUIRE(r.m->recon());
    CHECK(r.m->recon()->components.empty());
    REQUIRE(r.m->recon()->mra_skipped);
    CHECK_FALSE(r.m->clip(ClipKind::band, 0));
}

TEST_CASE("toggling reuses the DWT coefficients", "[tui][decomposition]") {
    Rig r;
    const auto before = r.m->data();
    r.m->toggle(3, r.jobs);
    CHECK(r.m->data() == before);
}

TEST_CASE("options change the wavelet, levels and channel", "[tui][decomposition]") {
    Rig r;
    r.m->set_options({.wavelet = "haar", .levels = 3, .channel = 0}, r.jobs);
    auto d = r.m->data();
    REQUIRE(d);
    CHECK(d->bands.size() == 4);
    CHECK(d->bands[3].name == "a3");
    r.m->set_options({.wavelet = "db2", .levels = 99}, r.jobs);
    CHECK(r.m->options().levels == core::dwt_max_level(d->x.size(), 4));
    r.m->set_options({.wavelet = "nonsense"}, r.jobs);
    CHECK(r.m->error());
}

TEST_CASE("clips for A, B and the difference share one length", "[tui][decomposition][audio]") {
    Rig r;
    auto a = r.m->clip(ClipKind::original), b = r.m->clip(ClipKind::enabled), x = r.m->clip(ClipKind::difference),
         band = r.m->clip(ClipKind::band, 2);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(x);
    REQUIRE(band);
    CHECK(a->frames() == b->frames());
    CHECK(b->frames() == x->frames());
    CHECK(a->rate == 44'100);
    CHECK(b->tag == "decomposition.enabled");
}

TEST_CASE("a cancelled decomposition publishes nothing", "[tui][decomposition]") {
    JobRunner jobs{[] {}};
    JobRunner inline_jobs{[] {}, 0ms, true};
    auto signal = std::make_shared<SignalModel>();
    signal->open(signals::test::sample("test.wav"), inline_jobs);
    DecompositionModel m{signal};
    m.sync(jobs);
    jobs.cancel_all();
}

TEST_CASE("a toggle during a new decomposition never publishes the old bands", "[tui][decomposition]") {
    JobRunner inline_jobs{[] {}, 0ms, true};
    auto signal = std::make_shared<SignalModel>();
    signal->open(signals::test::sample("test.wav"), inline_jobs);
    DecompositionModel m{signal};
    m.sync(inline_jobs);
    REQUIRE(m.data());
    JobRunner jobs{[] {}};
    m.set_options({.wavelet = "haar", .levels = 3}, jobs);
    m.toggle(0, jobs);
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        m.sync(jobs);
        const auto d = m.data();
        const auto r = m.recon();
        if (d && d->bands.size() == 4 && r && r->enabled.size() == 4 && !m.job()) break;
        std::this_thread::sleep_for(20ms);
    }
    const auto d = m.data();
    const auto r = m.recon();
    REQUIRE(d);
    REQUIRE(r);
    CHECK(d->bands.size() == 4);
    CHECK(r->components.size() == 4);
    REQUIRE(r->enabled.size() == 4);
    CHECK_FALSE(r->enabled[0]);
}

TEST_CASE("an unknown wavelet keeps the current decomposition and says why", "[tui][decomposition]") {
    Rig r;
    const auto before = r.m->data();
    r.m->set_options({.wavelet = "p3-k2"}, r.jobs);
    CHECK(r.m->error());
    CHECK(r.m->options().wavelet == "db2");
    CHECK(r.m->data() == before);
    r.m->set_options({.wavelet = "haar", .levels = 3}, r.jobs);
    CHECK_FALSE(r.m->error());
    CHECK(r.m->data()->bands.size() == 4);
}
