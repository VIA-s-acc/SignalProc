#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <functional>
#include <numbers>
#include <set>
#include <string>
#include <thread>

#include "core/design/families.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/filters_model.hpp"

using namespace signals::tui;
using Kind = FilterRef::Kind;

namespace {

const LibraryNode* child(const LibraryNode& node, const std::string& label) {
    for (const auto& c : node.children)
        if (c.label == label) return &c;
    return nullptr;
}

void collect(const LibraryNode& node, std::vector<FilterRef>& out) {
    if (node.ref) out.push_back(*node.ref);
    for (const auto& c : node.children) collect(c, out);
}

}  // namespace

TEST_CASE("library lists every wavelet once and marks later groups", "[tui][filters]") {
    const auto lib = build_library();
    const auto* wavelets = child(lib, "Вейвлеты");
    REQUIRE(wavelets);
    std::vector<FilterRef> refs;
    collect(*wavelets, refs);
    std::set<std::string> names;
    for (const auto& r : refs) {
        CHECK(r.kind == Kind::bank);
        names.insert(r.wavelet);
    }
    CHECK(refs.size() == signals::core::wavelet_names().size());
    CHECK(names.size() == refs.size());
    for (auto n : signals::core::wavelet_names()) CHECK(names.count(std::string{n}) == 1);

    const auto* equivalent = child(lib, "Эквивалентные (db2, J = 3)");
    REQUIRE(equivalent);
    std::vector<FilterRef> eq;
    collect(*equivalent, eq);
    REQUIRE(eq.size() == 4);
    CHECK(eq[0].label() == "a₃ db2");
    CHECK(eq[1].label() == "d₃ db2");
    CHECK(eq[3].label() == "d₁ db2");

    const auto* chain = child(lib, "Цепочка: T(z), A(z)");
    REQUIRE(chain);
    std::vector<FilterRef> ch;
    collect(*chain, ch);
    REQUIRE(ch.size() == 2);
    CHECK(ch[0].label() == "T(z) db2");
    CHECK(ch[1].label() == "A(z) db2");

    int later = 0;
    for (const auto& g : lib.children) {
        if (g.later.empty()) continue;
        ++later;
        CHECK(g.later == (g.label == "Мои фильтры" ? std::string{"пусто (Банк)"} : std::string{"M6"}));
        std::vector<FilterRef> none;
        collect(g, none);
        CHECK(none.empty());
    }
    CHECK(later == 4);
}

TEST_CASE("filter refs label bank roles with subscripts", "[tui][filters]") {
    CHECK(FilterRef{Kind::bank, "db2", FilterRole::h0}.label() == "h₀ db2");
    CHECK(FilterRef{Kind::bank, "sym4", FilterRole::f1}.label() == "f₁ sym4");
    CHECK(FilterRef{Kind::equivalent, "db2", FilterRole::h0, 2, true}.label() == "d₂ db2");
}

TEST_CASE("db2 lowpass analysis matches the cli numbers", "[tui][filters]") {
    const auto a = analyze({Kind::bank, "db2", FilterRole::h0}, 4096);
    REQUIRE(a);
    CHECK(a->h.coeffs.size() == 4);
    CHECK(a->response.omega.size() == 4096);
    CHECK(a->metrics.cutoff_3db == Catch::Approx(0.5).margin(1e-3));
    CHECK(std::isfinite(a->metrics.passband_ripple_db));
    CHECK(std::isfinite(a->metrics.stopband_attenuation_db));
    CHECK(a->zeros_at_minus_one == 2);
    REQUIRE(a->pr_error);
    CHECK(*a->pr_error < 1e-12);
    REQUIRE(a->orth_error);
    CHECK(*a->orth_error < 1e-12);
    REQUIRE(a->sobolev);
    CHECK(*a->sobolev > 0.5);
    CHECK_FALSE(a->linear_phase);
    REQUIRE(a->zp);
    CHECK(a->zp->zeros.size() == 3);
}

TEST_CASE("band-pass and chain filters have no pass-band metrics", "[tui][filters]") {
    const auto d2 = analyze({Kind::equivalent, "db2", FilterRole::h0, 2, true}, 4096);
    REQUIRE(d2);
    CHECK(std::isnan(d2->metrics.passband_ripple_db));
    CHECK(std::isnan(d2->metrics.stopband_attenuation_db));
    std::size_t peak = 0;
    for (std::size_t k = 0; k < d2->response.magnitude.size(); ++k)
        if (d2->response.magnitude[k] > d2->response.magnitude[peak]) peak = k;
    const double at = d2->response.omega[peak] / std::numbers::pi;
    CHECK(at >= 0.25);
    CHECK(at <= 0.5);

    const auto t = analyze({Kind::distortion, "db2"}, 1024);
    REQUIRE(t);
    const auto [lo, hi] = std::minmax_element(t->response.magnitude.begin(), t->response.magnitude.end());
    CHECK(*hi - *lo < 1e-12);
    CHECK(std::isnan(t->metrics.passband_ripple_db));

    const auto alias = analyze({Kind::alias, "db2"}, 1024);
    REQUIRE(alias);
    CHECK(*std::max_element(alias->response.magnitude_db.begin(), alias->response.magnitude_db.end()) < -200);
}

TEST_CASE("biorthogonal and linear-phase banks report what applies", "[tui][filters]") {
    const auto a = analyze({Kind::bank, "bior2.2", FilterRole::h0}, 512);
    REQUIRE(a);
    CHECK(a->linear_phase);
    CHECK_FALSE(a->orth_error);
    REQUIRE(a->pr_error);
    CHECK(*a->pr_error < 1e-12);
    CHECK_FALSE(analyze({Kind::bank, "nope", FilterRole::h0}, 512));
}

TEST_CASE("comparison holds at most five filters", "[tui][filters]") {
    ComparisonSet set;
    const std::vector<FilterRef> refs{{Kind::bank, "db2", FilterRole::h0}, {Kind::bank, "db2", FilterRole::h1},
                                      {Kind::equivalent, "db2", FilterRole::h0, 2, true}, {Kind::bank, "sym4", FilterRole::h0},
                                      {Kind::distortion, "db2"}};
    for (const auto& r : refs) CHECK(set.add(r));
    const auto sixth = set.add({Kind::bank, "coif1", FilterRole::h0});
    REQUIRE_FALSE(sixth);
    CHECK(sixth.error().code == signals::core::Errc::invalid_argument);
    CHECK(set.add(refs[0]));
    CHECK(set.entries().size() == 5);

    set.toggle(1);
    CHECK_FALSE(set.entries()[1].visible);
    set.set_reference(3);
    CHECK(set.reference() == 3u);
    set.set_focus(2);
    CHECK(set.focus() == 2u);
    set.remove(0);
    CHECK(set.entries().size() == 4);
    CHECK(set.entries()[0].ref == refs[1]);
    CHECK(set.reference() == 2u);
    CHECK(set.focus() == 1u);
    set.remove(2);
    CHECK_FALSE(set.reference());
}

TEST_CASE("comparison analyses arrive through a job", "[tui][filters]") {
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    ComparisonSet set;
    REQUIRE(set.add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(set.add({Kind::equivalent, "db2", FilterRole::h0, 3, false}));
    CHECK_FALSE(set.entries()[0].analysis);
    set.request(jobs, 256);
    for (const auto& e : set.entries()) {
        REQUIRE(e.analysis);
        CHECK(e.analysis->response.omega.size() == 256);
    }
    const auto view = jobs.snapshot().back();
    CHECK(view.stages.size() == 2);
    const auto before = jobs.snapshot().size();
    set.request(jobs, 256);
    CHECK(jobs.snapshot().size() == before);
}

TEST_CASE("range response matches the full response on a sub-band", "[tui][filters]") {
    const auto bank = signals::core::wavelet("db2");
    REQUIRE(bank);
    const auto full = signals::core::frequency_response(bank->h0, 1024);
    const auto part = range_response(bank->h0, 0.25, 0.5, 257, &full);
    REQUIRE(part.omega.size() == 257);
    CHECK(part.omega.front() == Catch::Approx(0.25 * std::numbers::pi));
    CHECK(part.omega.back() == Catch::Approx(0.5 * std::numbers::pi));
    const std::size_t k = 256;
    CHECK(part.magnitude_db.front() == Catch::Approx(full.magnitude_db[k]).margin(1e-9));
    CHECK(part.phase_unwrapped.front() == Catch::Approx(full.phase_unwrapped[k]).margin(1e-9));
    CHECK(part.group_delay.front() == Catch::Approx(full.group_delay[k]).margin(1e-9));
}

TEST_CASE("group delay is masked where the magnitude is tiny", "[tui][filters]") {
    const auto a = analyze({Kind::bank, "db2", FilterRole::h1}, 1024);
    REQUIRE(a);
    const auto gd = masked_group_delay(a->response);
    CHECK(std::isnan(gd.front()));
    CHECK(std::isfinite(gd[512]));
    CHECK(std::isfinite(a->response.group_delay[1]));
    CHECK(std::isnan(gd[1]));
}

TEST_CASE("pending analyses are not submitted twice", "[tui][filters]") {
    std::function<void()> notify = [] {};
    JobRunner jobs{notify};
    ComparisonSet set;
    REQUIRE(set.add({Kind::bank, "sym17", FilterRole::h0}));
    set.request(jobs, 4096);
    set.request(jobs, 4096);
    std::size_t submitted = 0;
    for (const auto& v : jobs.snapshot()) submitted += v.title == "Анализ фильтров";
    CHECK(submitted == 1);
    for (int k = 0; k < 30000 && !set.entries()[0].analysis; ++k) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    CHECK(set.entries()[0].analysis);
    CHECK_FALSE(set.pending_label());
}

TEST_CASE("view responses cover the zoomed band", "[tui][filters]") {
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    ComparisonSet set;
    REQUIRE(set.add({Kind::bank, "db2", FilterRole::h0}));
    set.request(jobs, 1024);
    set.request_view(jobs, 0.4, 0.6, 512);
    const auto e = set.entries()[0];
    REQUIRE(e.view);
    CHECK(e.view_lo == 0.4);
    CHECK(e.view_hi == 0.6);
    CHECK(e.view->omega.size() == 512);
    CHECK_FALSE(set.view_pending());
}
