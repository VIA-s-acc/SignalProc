#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/design/families.hpp"
#include "io/npy.hpp"
#include "pipeline/block.hpp"
#include "pipeline/npz_values.hpp"
#include "support/banks.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals;
using namespace signals::pipeline;

std::filesystem::path npy_fixture(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "npy" / name;
}

io::NpzEntries fixture_entries(const std::string& name) {
    auto e = io::read_npz(npy_fixture(name));
    REQUIRE(e);
    return *e;
}

const std::vector<double>& numbers(const io::NpzEntries& e, const std::string& key) {
    const auto* a = io::np_find(e, key);
    INFO(key);
    REQUIRE(a);
    return std::get<std::vector<double>>(a->data);
}

std::string text(const io::NpzEntries& e, const std::string& key) {
    const auto* a = io::np_find(e, key);
    INFO(key);
    REQUIRE(a);
    return std::get<std::vector<std::string>>(a->data).at(0);
}

double gap(const std::vector<double>& a, const std::vector<double>& b) {
    REQUIRE(a.size() == b.size());
    double worst = 0, peak = 1;
    for (std::size_t i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - b[i])), peak = std::max(peak, std::abs(b[i]));
    return worst / peak;
}

Value run_block(const std::string& id, std::vector<Value> in, const Params& given) {
    const auto* t = find_block_type(id);
    REQUIRE(t);
    const auto params = resolve_params(*t, given);
    REQUIRE(params);
    RunContext ctx;
    auto out = t->run(ctx, in, *params);
    INFO((out ? std::string{} : out.error().message));
    REQUIRE(out);
    return out->front();
}

Coeffs coeffs_of(const std::string& id, const core::AudioBuffer& x, const Params& p) { return std::get<Coeffs>(run_block(id, {Value{x}}, p)); }

core::AudioBuffer fixture_signal(const std::string& name, double rate) { return {rate, {numbers(fixture_entries(name), "signal")}}; }

core::AudioBuffer stereo(std::size_t n) {
    core::AudioBuffer a{8000, {std::vector<double>(n), std::vector<double>(n)}};
    for (std::size_t k = 0; k < n; ++k) {
        a.channels[0][k] = std::sin(0.05 * static_cast<double>(k)) + 0.1 * std::cos(1.3 * static_cast<double>(k));
        a.channels[1][k] = std::cos(0.02 * static_cast<double>(k)) - 0.2 * std::sin(2.1 * static_cast<double>(k));
    }
    return a;
}

void check_same(const Coeffs& a, const Coeffs& b) {
    REQUIRE(a.channels.size() == b.channels.size());
    for (std::size_t c = 0; c < a.channels.size(); ++c) {
        REQUIRE(a.channels[c].bands.size() == b.channels[c].bands.size());
        for (std::size_t k = 0; k < a.channels[c].bands.size(); ++k) CHECK(a.channels[c].bands[k] == b.channels[c].bands[k]);
    }
    CHECK(a.lengths == b.lengths);
    CHECK(a.wavelet == b.wavelet);
    CHECK(a.mode == b.mode);
    CHECK(a.transform == b.transform);
    CHECK(a.norm == b.norm);
    CHECK(a.rate == b.rate);
    CHECK(a.basis == b.basis);
}

}  // namespace

TEST_CASE("dwt coefficients are written in pywt sign with pywt keys", "[npz-values]") {
    const auto ref = fixture_entries("pywt_dwt.npz");
    const auto c = coeffs_of("dwt", fixture_signal("pywt_dwt.npz", 1000), {{"wavelet", "db2"}, {"levels", "3"}, {"ext", "symmetric"}});
    const auto e = coeffs_entries(c);
    REQUIRE(e);
    for (const std::string key : {"cA3", "cD3", "cD2", "cD1"}) {
        INFO(key);
        CHECK(gap(numbers(*e, key), numbers(ref, key)) <= 1e-12);
    }
    auto negated = c.channels[0].bands[3];
    for (auto& v : negated) v = -v;
    CHECK(numbers(*e, "cD1") == negated);
    CHECK(numbers(*e, "cA3") == c.channels[0].bands[0]);
    CHECK(text(*e, "detail_sign") == "pywt");
    CHECK(text(*e, "wavelet") == "db2");
    CHECK(text(*e, "mode") == "symmetric");
    CHECK(text(*e, "transform") == "dwt");
    CHECK(numbers(*e, "length") == std::vector<double>{1000});
    CHECK(numbers(*e, "rate") == std::vector<double>{1000});
    CHECK(band_keys(c) == std::vector<std::string>{"cA3", "cD3", "cD2", "cD1"});
}

TEST_CASE("a pywt file without detail_sign opens in Signals sign", "[npz-values]") {
    const auto c = coeffs_of("dwt", fixture_signal("pywt_dwt.npz", 1000), {{"wavelet", "db2"}, {"levels", "3"}, {"ext", "symmetric"}});
    const auto back = coeffs_from_entries(fixture_entries("pywt_dwt.npz"), {});
    INFO((back ? std::string{} : back.error().message));
    REQUIRE(back);
    REQUIRE(back->channels.size() == 1);
    REQUIRE(back->channels[0].bands.size() == 4);
    for (std::size_t k = 0; k < 4; ++k) CHECK(gap(back->channels[0].bands[k], c.channels[0].bands[k]) <= 1e-12);
    CHECK(back->wavelet == "db2");
    CHECK(back->transform == "dwt");
    const auto bare = coeffs_from_entries(fixture_entries("pywt_dwt_bare.npz"), {});
    REQUIRE_FALSE(bare);
    CHECK(bare.error().message.find("wavelet") != std::string::npos);
    const auto hinted = coeffs_from_entries(fixture_entries("pywt_dwt_bare.npz"), {"db2", "symmetric", 1000.0});
    REQUIRE(hinted);
    CHECK(gap(hinted->channels[0].bands[1], c.channels[0].bands[1]) <= 1e-12);
    CHECK(hinted->rate == 1000);
}

TEST_CASE("swt and wpt follow pywt keys and sign", "[npz-values]") {
    const auto swt_ref = fixture_entries("pywt_swt.npz");
    const auto s = coeffs_of("swt", fixture_signal("pywt_swt.npz", 1024), {{"wavelet", "db2"}, {"levels", "2"}});
    const auto se = coeffs_entries(s);
    REQUIRE(se);
    for (const std::string key : {"cA2", "cD2", "cD1"}) {
        INFO(key);
        CHECK(gap(numbers(*se, key), numbers(swt_ref, key)) <= 1e-12);
    }
    CHECK(numbers(*se, "norm") == std::vector<double>{0});
    const auto wpt_ref = fixture_entries("pywt_wpt.npz");
    const auto w = coeffs_of("wpt", fixture_signal("pywt_wpt.npz", 1000), {{"wavelet", "db2"}, {"levels", "2"}, {"basis", "full"}, {"ext", "symmetric"}});
    const auto we = coeffs_entries(w);
    REQUIRE(we);
    for (const std::string key : {"aa", "ad", "da", "dd"}) {
        INFO(key);
        CHECK(gap(numbers(*we, key), numbers(wpt_ref, key)) <= 1e-12);
    }
    CHECK(text(*we, "detail_sign") == "pywt");
    CHECK(text(*we, "transform") == "wpt");
}

TEST_CASE("lifting and dtcwt keep Signals sign", "[npz-values]") {
    const auto x = stereo(512);
    const auto li = coeffs_of("lift53", x, {{"levels", "4"}, {"integer", "yes"}});
    const auto le = coeffs_entries(li);
    REQUIRE(le);
    CHECK(text(*le, "transform") == "lift53i");
    CHECK(text(*le, "detail_sign") == "signals");
    for (std::size_t k = 0; k < li.channels[1].bands[2].size(); ++k)
        CHECK(numbers(*le, "cD3")[li.channels[1].bands[2].size() + k] == li.channels[1].bands[2][k]);
    const auto l97 = coeffs_entries(coeffs_of("lift97", x, {{"levels", "3"}}));
    REQUIRE(l97);
    CHECK(text(*l97, "transform") == "lift97");
    const auto d = coeffs_of("dtcwt", x, {{"levels", "3"}});
    const auto de = coeffs_entries(d);
    REQUIRE(de);
    CHECK(text(*de, "detail_sign") == "signals");
    CHECK(io::np_find(*de, "cA3"));
    for (const std::string key : {"re_cD3", "im_cD3", "re_cD1", "im_cD1"}) CHECK(io::np_find(*de, key));
    CHECK(numbers(*de, "re_cD1").size() == d.channels[0].bands[3].size());
}

TEST_CASE("coefficients survive a round trip through npz", "[npz-values]") {
    const auto x = stereo(512);
    const std::vector<std::pair<std::string, Params>> cases{{"dwt", {{"wavelet", "sym4"}, {"levels", "3"}, {"ext", "periodization"}}},
                                                            {"swt", {{"wavelet", "db2"}, {"levels", "3"}, {"norm", "yes"}}},
                                                            {"wpt", {{"wavelet", "db3"}, {"levels", "3"}, {"basis", "best"}}},
                                                            {"lift53", {{"levels", "3"}}},
                                                            {"lift53", {{"levels", "3"}, {"integer", "yes"}}},
                                                            {"lift97", {{"levels", "3"}}},
                                                            {"dtcwt", {{"levels", "3"}}}};
    for (const auto& [id, p] : cases) {
        INFO(id);
        const auto c = coeffs_of(id, x, p);
        const auto e = coeffs_entries(c);
        REQUIRE(e);
        CHECK(io::np_find(*e, band_keys(c).at(0))->shape.size() == 2);
        const auto path = signals::test::temp_path("round-" + id + ".npz");
        REQUIRE(io::write_npz(path, *e));
        const auto read = io::read_npz(path);
        REQUIRE(read);
        const auto back = coeffs_from_entries(*read, {});
        INFO((back ? std::string{} : back.error().message));
        REQUIRE(back);
        check_same(*back, c);
    }
}

TEST_CASE("an unknown detail_sign is an error", "[npz-values]") {
    auto e = fixture_entries("pywt_dwt.npz");
    e.emplace_back("detail_sign", io::np_text("+1"));
    const auto c = coeffs_from_entries(e, {});
    REQUIRE_FALSE(c);
    CHECK(c.error().message.find("detail_sign") != std::string::npos);
}

TEST_CASE("report entries keep field order and passed", "[npz-values]") {
    Report r;
    r.fields = {{"snr_db", 12.5}, {"kind", std::string{"snr"}}, {"n", 3.0}};
    r.passed = false;
    const auto e = report_entries(r);
    CHECK(numbers(e, "snr_db") == std::vector<double>{12.5});
    CHECK(text(e, "kind") == "snr");
    CHECK(numbers(e, "passed") == std::vector<double>{0});
    CHECK(std::get<std::vector<std::string>>(io::np_find(e, "fields")->data) == std::vector<std::string>{"snr_db", "kind", "n"});
    const auto v = value_entries(Value{r});
    REQUIRE(v);
    CHECK(v->size() == e.size());
    CHECK_FALSE(value_entries(Value{2.0}));
    const auto pre = prefixed("probe", e);
    CHECK(pre.front().first == "probe.snr_db");
}

TEST_CASE("bank entries equal the pywt filter bank", "[npz-values]") {
    for (const std::string name : {"db2", "bior2.2", "sym8"}) {
        INFO(name);
        const auto bank = core::wavelet(name);
        REQUIRE(bank);
        const auto e = bank_entries(*bank);
        const auto& g = signals::test::golden_wavelet(name);
        const double tol = name.starts_with("sym") ? 1e-10 : 1e-15;
        for (const std::string key : {"dec_lo", "dec_hi", "rec_lo", "rec_hi"}) {
            INFO(key);
            const auto want = g[key].get<std::vector<double>>();
            const auto& got = numbers(e, key);
            REQUIRE(got.size() == want.size());
            double worst = 0;
            for (std::size_t i = 0; i < got.size(); ++i) worst = std::max(worst, std::abs(got[i] - want[i]));
            CHECK(worst <= tol);
        }
        CHECK(text(e, "name") == name);
    }
}
