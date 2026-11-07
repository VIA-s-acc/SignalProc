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
    CHECK(later == 1);
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

TEST_CASE("the classical group designs Butterworth, Chebyshev, elliptic, Bessel and FIR filters", "[tui][filters][classic]") {
    const auto lib = build_library();
    const auto* classic = child(lib, "Классические КИХ / БИХ");
    REQUIRE(classic);
    std::vector<FilterRef> refs;
    collect(*classic, refs);
    REQUIRE(refs.size() == 10);
    for (const auto& r : refs) {
        INFO(r.label());
        CHECK(r.kind == Kind::classical);
        const auto a = analyze(r, 512);
        REQUIRE(a);
        CHECK(a->response.omega.size() == 512);
        if (a->zpk) CHECK(a->stable.value_or(false));
    }
    CHECK(refs[0].label() == "Баттерворт ФНЧ 4 · 0,25π");
    const auto butter = analyze(refs[0], 512);
    REQUIRE(butter);
    CHECK(butter->response.magnitude[128] == Catch::Approx(std::sqrt(0.5)).margin(1e-9));
    CHECK(butter->response.magnitude[0] == Catch::Approx(1).margin(1e-9));
    REQUIRE(butter->zp);
    CHECK(butter->zp->poles.size() == 4);
    double dc = 0;
    for (double v : butter->h.coeffs) dc += v;
    CHECK(dc == Catch::Approx(1).margin(1e-3));
    const auto remez = analyze(refs[8], 512);
    REQUIRE(remez);
    CHECK(remez->h.coeffs.size() == 31);
    CHECK(remez->linear_phase);
}

TEST_CASE("classical steps change the order and the cutoff", "[tui][filters][classic]") {
    ClassicalSpec s;
    const auto ref = classical_ref(s);
    const auto up = classical_step(ref, 1, 1.0);
    REQUIRE(up);
    CHECK(parse_classical(up->wavelet)->order == 5);
    const auto wide = classical_step(ref, 0, 1.1);
    REQUIRE(wide);
    CHECK(parse_classical(wide->wavelet)->edges[0] == Catch::Approx(0.275));
    ClassicalSpec fir;
    fir.method = ClassicalSpec::Method::remez;
    fir.order = 31;
    fir.edges = {0.4, 0.5};
    CHECK(parse_classical(classical_step(classical_ref(fir), 1, 1.0)->wavelet)->order == 33);
    CHECK_FALSE(classical_step(FilterRef{Kind::bank, "db2"}, 1, 1.0));
    CHECK_FALSE(parse_classical("iir/oops/lowpass/4/0.25"));
}

TEST_CASE("edge steps keep classical band edges apart at the limits", "[tui][filters][classic]") {
    using M = ClassicalSpec::Method;
    using T = signals::core::BandType;
    for (const auto& spec : {ClassicalSpec{M::iir, signals::core::IirFamily::butterworth, T::bandpass, 4, {0.2, 0.4}},
                             ClassicalSpec{M::iir, signals::core::IirFamily::butterworth, T::bandstop, 4, {0.3, 0.5}},
                             ClassicalSpec{M::remez, signals::core::IirFamily::butterworth, T::lowpass, 31, {0.4, 0.5}}}) {
        auto ref = classical_ref(spec);
        INFO(ref.label());
        for (const auto& [factor, presses] : {std::pair{1.1, 40}, std::pair{1 / 1.1, 40}, std::pair{1.1, 25}}) {
            for (int k = 0; k < presses; ++k) {
                const auto next = classical_step(ref, 0, factor);
                REQUIRE(next);
                ref = *next;
                const auto edges = parse_classical(ref.wavelet)->edges;
                CHECK(edges.front() >= 0.01);
                CHECK(edges.back() <= 0.98);
                for (std::size_t i = 1; i < edges.size(); ++i) CHECK(edges[i] > edges[i - 1] * 1.05);
            }
            if (factor < 1) continue;
            const auto a = analyze(ref, 256);
            INFO((a ? std::string{} : a.error().message));
            CHECK(a);
        }
    }
}

TEST_CASE("every library group has filters or says when they come", "[tui][filters]") {
    const auto lib = build_library();
    for (const auto& g : lib.children) {
        INFO(g.label);
        CHECK_FALSE(g.label.empty());
        CHECK((!g.children.empty() || !g.later.empty()));
    }
}

TEST_CASE("a zoomed IIR view continues the unwrapped phase of the full band", "[tui][filters][classic]") {
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    ComparisonSet set;
    REQUIRE(set.add(classical_ref(ClassicalSpec{ClassicalSpec::Method::iir, signals::core::IirFamily::butterworth, signals::core::BandType::lowpass, 8, {0.25}})));
    set.request(jobs, 1024);
    set.request_view(jobs, 0.5, 0.75, 257);
    const auto e = set.entries()[0];
    REQUIRE(e.analysis);
    REQUIRE(e.view);
    CHECK(e.view->phase_unwrapped.front() == Catch::Approx(e.analysis->response.phase_unwrapped[512]).margin(1e-9));
}

TEST_CASE("scaling and wavelet spectra span 8π and pass the partition checks", "[tui][filters][spectra]") {
    constexpr double pi = std::numbers::pi;
    const auto phi = analyze({Kind::scaling, "db4", FilterRole::f0}, 512);
    REQUIRE(phi);
    CHECK(phi->span == 8);
    CHECK(phi->response.omega.size() == 4 * 512);
    CHECK(phi->response.omega.back() == Catch::Approx(8 * pi * (4 * 512 - 1) / (4 * 512)));
    CHECK(phi->response.magnitude.front() == Catch::Approx(1).margin(1e-12));
    REQUIRE(phi->partition_error);
    CHECK(*phi->partition_error < 1e-6);
    REQUIRE(phi->factors);
    CHECK(*phi->factors >= 34);
    CHECK(phi->zeros_at_minus_one == 4);
    const auto psi = analyze({Kind::wavelet_fn, "db4", FilterRole::f0}, 512);
    REQUIRE(psi);
    CHECK(psi->response.magnitude.front() < 1e-12);
    REQUIRE(psi->partition_error);
    CHECK(*psi->partition_error < 1e-6);
    const auto dual = analyze({Kind::scaling, "bior2.2", FilterRole::h0}, 512);
    REQUIRE(dual);
    CHECK_FALSE(dual->partition_error);
    CHECK(dual->response.magnitude.front() == Catch::Approx(1).margin(1e-12));
    const auto three = analyze({Kind::scaling, "db4", FilterRole::f0, 3}, 512);
    REQUIRE(three);
    CHECK(three->factors == 3);
    CHECK(std::abs(three->response.h[300] - phi->response.h[300]) > 1e-3);
}

TEST_CASE("cwt spectra keep a constant quality factor over scale", "[tui][filters][spectra]") {
    constexpr double pi = std::numbers::pi;
    for (const std::size_t a : {4, 16}) {
        const auto m = analyze({Kind::cwt, "morl", FilterRole::h0, a}, 1024);
        REQUIRE(m);
        CHECK(m->span == 1);
        REQUIRE(m->centre);
        CHECK(*m->centre == Catch::Approx(5.0 / (pi * static_cast<double>(a))).epsilon(1e-3));
        REQUIRE(m->quality);
        CHECK(*m->quality == Catch::Approx(5.0 / (2 * std::sqrt(2 * std::log(std::sqrt(2.0))))).epsilon(1e-3));
    }
    const auto mexh = analyze({Kind::cwt, "mexh", FilterRole::h0, 8}, 1024);
    REQUIRE(mexh);
    REQUIRE(mexh->at_zero);
    CHECK(*mexh->at_zero < 1e-15);
    CHECK_FALSE(analyze({Kind::cwt, "db2", FilterRole::h0, 8}, 64));
}

TEST_CASE("packet node analyses sit in their frequency band", "[tui][filters][spectra]") {
    const auto lib = build_library();
    const LibraryNode* packets = nullptr;
    for (const auto& g : lib.children)
        if (g.label.starts_with("Узлы пакетов")) packets = &g;
    REQUIRE(packets);
    std::vector<FilterRef> refs;
    collect(*packets, refs);
    REQUIRE(refs.size() == 2 + 4 + 8);
    for (const auto& r : refs) CHECK(r.kind == Kind::packet);
    auto ref = refs.back();
    ref.wavelet = "db8";
    const auto a = analyze(ref, 1024);
    REQUIRE(a);
    const auto& m = a->response.magnitude;
    const double peak = static_cast<double>(std::max_element(m.begin(), m.end()) - m.begin()) / 1024.0;
    CHECK(peak > 7.0 / 8 - 0.01);
    CHECK(a->h.coeffs.size() > 16);
}

TEST_CASE("the spectra group offers φ, ψ and their duals", "[tui][filters][spectra]") {
    const auto lib = build_library();
    const LibraryNode* spectra = nullptr;
    const LibraryNode* cwt = nullptr;
    for (const auto& g : lib.children) {
        if (g.label.starts_with("Спектры φ / ψ")) spectra = &g;
        if (g.label.starts_with("CWT")) cwt = &g;
    }
    REQUIRE(spectra);
    REQUIRE(cwt);
    CHECK(spectra->later.empty());
    CHECK(cwt->later.empty());
    std::vector<FilterRef> s, c;
    collect(*spectra, s);
    collect(*cwt, c);
    REQUIRE(s.size() == 4);
    CHECK(s[0].kind == Kind::scaling);
    CHECK(s[1].kind == Kind::wavelet_fn);
    CHECK(c.size() == 15);
    for (const auto& r : c) CHECK(analyze(r, 128));
    FilterRef phi = s[0];
    phi.wavelet = "db4";
    CHECK(phi.label() == "Φ db4");
    const auto down = spectrum_step(phi, -1);
    REQUIRE(down);
    CHECK(down->level == 12);
    CHECK(down->label() == "Φ db4 · J=12");
    CHECK(spectrum_step(*down, 1)->level == 0);
    CHECK(spectrum_step(FilterRef{Kind::scaling, "db4", FilterRole::f0, 1}, -1)->level == 1);
    CHECK_FALSE(spectrum_step(FilterRef{Kind::bank, "db4"}, 1));
}

TEST_CASE("a spectrum analysis is not redone once it is current", "[tui][filters][spectra]") {
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    ComparisonSet set;
    REQUIRE(set.add({Kind::scaling, "db2", FilterRole::f0}));
    set.request(jobs, 256);
    const auto first = set.entries()[0].analysis;
    REQUIRE(first);
    set.request(jobs, 256);
    CHECK(set.entries()[0].analysis == first);
    set.request(jobs, 512);
    CHECK(set.entries()[0].analysis != first);
    CHECK(span_of({Kind::scaling, "db4", FilterRole::f0}) == 8);
    CHECK(span_of({Kind::wavelet_fn, "db4", FilterRole::h0}) == 8);
    CHECK(span_of({Kind::cwt, "morl", FilterRole::h0, 8}) == 1);
    CHECK(span_of({Kind::bank, "db4", FilterRole::h0}) == 1);
}
