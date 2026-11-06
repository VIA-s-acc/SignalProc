#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <vector>

#include "core/design/families.hpp"
#include "core/laurent.hpp"
#include "tui/screens/designer_model.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;
namespace core = signals::core;

namespace {

std::vector<double> trimmed(const core::Laurent& h) { return core::trim(h, 1e-14).coeffs; }

void same(const core::Laurent& a, const core::Laurent& b) {
    const auto x = trimmed(a), y = trimmed(b);
    REQUIRE(x.size() == y.size());
    for (std::size_t i = 0; i < x.size(); ++i) CHECK(x[i] == Catch::Approx(y[i]).margin(1e-12));
}

struct Rig {
    JobRunner jobs{[] {}, 0ms, true};
    DesignerModel m;
    explicit Rig(int p = 3) { m.set_p(p, jobs); }
};

}  // namespace

TEST_CASE("p = 3 has 28 splits and the Daubechies preset is db3", "[tui][designer]") {
    Rig r;
    REQUIRE(r.m.data());
    CHECK(r.m.data()->rows.size() == 28);
    CHECK(r.m.data()->f.groups.size() == 2);
    r.m.preset(Preset::daubechies, r.jobs);
    auto res = r.m.result();
    REQUIRE(res);
    CHECK(res->title.starts_with("db3"));
    same(res->bank.h0, core::wavelet("db3")->f0);
    CHECK(res->delay == 5);
    CHECK(res->orthogonal);
    CHECK_FALSE(res->linear_phase);
    CHECK(res->info.wavelet);
    REQUIRE(res->phi);
    REQUIRE(res->phi_dual);
    CHECK(res->phi->norm_ratio < 1.02);
}

TEST_CASE("the default split after set_p is the Daubechies preset", "[tui][designer]") {
    Rig r;
    REQUIRE(r.m.result());
    CHECK(r.m.result()->title.starts_with("db3"));
}

TEST_CASE("presets give mirror, linear-phase and degenerate banks", "[tui][designer]") {
    Rig r;
    r.m.preset(Preset::max_phase, r.jobs);
    auto res = r.m.result();
    REQUIRE(res);
    CHECK(res->title.find("максимальная фаза") != std::string::npos);
    auto db3 = trimmed(core::wavelet("db3")->f0);
    auto h0 = trimmed(res->bank.h0);
    std::reverse(db3.begin(), db3.end());
    REQUIRE(h0.size() == db3.size());
    for (std::size_t i = 0; i < h0.size(); ++i) CHECK(h0[i] == Catch::Approx(db3[i]).margin(1e-12));
    r.m.preset(Preset::cdf, r.jobs);
    CHECK(r.m.result()->linear_phase);
    CHECK(r.m.result()->title.find("линейная фаза") != std::string::npos);
    CHECK(r.m.split().zeros_h0 == 2);
    r.m.preset(Preset::degenerate, r.jobs);
    CHECK_FALSE(r.m.result()->info.wavelet);
    REQUIRE(r.m.result()->phi);
    CHECK(r.m.result()->phi->norm_ratio > 1.3);
}

TEST_CASE("toggling a root group moves it between H0 and F0", "[tui][designer]") {
    Rig r;
    const auto before = r.m.split().group_in_h0;
    r.m.toggle_group(0, r.jobs);
    CHECK(r.m.split().group_in_h0[0] != before[0]);
    CHECK(r.m.split().group_in_h0[1] == before[1]);
    CHECK_FALSE(r.m.result()->title.starts_with("db3"));
    CHECK(r.m.result()->linear_phase);
    r.m.toggle_group(1, r.jobs);
    CHECK(r.m.result()->title.find("максимальная фаза") != std::string::npos);
    r.m.zeros(1, r.jobs);
    CHECK(r.m.result()->title.find("своё разбиение") != std::string::npos);
}

TEST_CASE("zeros at -1 stay between 0 and 2p", "[tui][designer]") {
    Rig r;
    r.m.zeros(10, r.jobs);
    CHECK(r.m.split().zeros_h0 == 6);
    r.m.zeros(-10, r.jobs);
    CHECK(r.m.split().zeros_h0 == 0);
}

TEST_CASE("constraints filter the visible splits and shape toggles", "[tui][designer]") {
    Rig r;
    r.m.set_constraints({.orthogonal = true}, r.jobs);
    CHECK(r.m.visible_rows().size() == 2);
    r.m.zeros(1, r.jobs);
    CHECK(r.m.split().zeros_h0 == 3);
    r.m.toggle_group(0, r.jobs);
    CHECK(r.m.result()->orthogonal);
    r.m.set_constraints({.linear_phase = true}, r.jobs);
    CHECK(r.m.visible_rows().size() == 14);
    r.m.set_constraints({.wavelet_only = true}, r.jobs);
    CHECK(r.m.visible_rows().size() == r.m.data()->wavelet_rows);
    CHECK(r.m.data()->wavelet_rows > 0);
    CHECK(r.m.data()->wavelet_rows <= 20);
}

TEST_CASE("step walks the visible splits and wraps", "[tui][designer]") {
    Rig r;
    r.m.set_constraints({.orthogonal = true}, r.jobs);
    const auto start = r.m.split();
    r.m.step(1, r.jobs);
    CHECK_FALSE(r.m.split().group_in_h0 == start.group_in_h0);
    r.m.step(1, r.jobs);
    CHECK(r.m.split().group_in_h0 == start.group_in_h0);
}

TEST_CASE("constraints that leave nothing give an empty list", "[tui][designer]") {
    Rig r{1};
    CHECK(r.m.data()->f.groups.empty());
    CHECK(r.m.data()->rows.size() == 3);
    r.m.set_constraints({.linear_phase = true, .orthogonal = true, .wavelet_only = true}, r.jobs);
    const auto before = r.m.split();
    r.m.step(1, r.jobs);
    CHECK(r.m.split().zeros_h0 == before.zeros_h0);
}

TEST_CASE("p = 5 enumerates every split", "[tui][designer]") {
    Rig r{5};
    const auto groups = r.m.data()->f.groups.size();
    CHECK(r.m.data()->rows.size() == 11 * (std::size_t{1} << groups));
    CHECK(r.m.result()->title.starts_with("db5"));
}

TEST_CASE("p is clamped to 1…8 and a pending enumeration can be cancelled", "[tui][designer]") {
    JobRunner jobs{[] {}};
    DesignerModel m;
    m.set_p(0, jobs);
    CHECK(m.p() == 1);
    m.set_p(99, jobs);
    CHECK(m.p() == 8);
    jobs.cancel_all();
}

TEST_CASE("x swaps analysis and synthesis", "[tui][designer]") {
    Rig r;
    const auto f0 = r.m.result()->bank.f0;
    r.m.swap(r.jobs);
    same(r.m.result()->bank.h0, f0);
}

TEST_CASE("cascade iterations stay between 1 and 8", "[tui][designer]") {
    Rig r;
    CHECK(r.m.cascade_iterations() == 8);
    r.m.set_cascade_iterations(20);
    CHECK(r.m.cascade_iterations() == 8);
    r.m.set_cascade_iterations(0);
    CHECK(r.m.cascade_iterations() == 2);
}

TEST_CASE("changing p to one with the same group count rebuilds the bank", "[tui][designer]") {
    Rig r{4};
    REQUIRE(r.m.result());
    REQUIRE(r.m.result()->title.starts_with("db4"));
    r.m.set_p(5, r.jobs);
    REQUIRE(r.m.result());
    CHECK(r.m.result()->title.starts_with("db5"));
    CHECK(core::trim(r.m.result()->bank.h0, 0.0).coeffs.size() == 10);
    r.m.set_p(4, r.jobs);
    CHECK(r.m.result()->title.starts_with("db4"));
}
