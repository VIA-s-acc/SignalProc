#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "core/design/families.hpp"
#include "i18n/i18n.hpp"
#include "tui/screens/explain_model.hpp"

using namespace signals::tui;

namespace {

const std::vector<double> kX{4, 6, 10, 12, 8, 6, 5, 5};

}  // namespace

TEST_CASE("haar walkthrough reproduces the textbook numbers", "[tui][explain]") {
    auto w = walkthrough(kX, {});
    REQUIRE(w);
    const double r = std::sqrt(2.0);
    const std::vector<double> y0{4, 10, 16, 22, 20, 14, 11, 10, 5}, y1{4, 2, 4, 2, -4, -2, -1, 0, -5};
    REQUIRE(w->y0.size() == 9);
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(w->y0[i] * r == Catch::Approx(y0[i]));
        CHECK(w->y1[i] * r == Catch::Approx(y1[i]).margin(1e-12));
    }
    CHECK(w->kept == std::vector<std::size_t>{1, 3, 5, 7});
    CHECK(w->a[0] * r == Catch::Approx(10));
    CHECK(w->a[1] * r == Catch::Approx(22));
    CHECK(w->d[2] * r == Catch::Approx(-2));
    const std::vector<double> w0{5, 5, 11, 11, 7, 7, 5, 5}, w1{-1, 1, -1, 1, 1, -1, 0, 0};
    CHECK(w->window == 0);
    for (std::size_t n = 0; n < 8; ++n) {
        CHECK(w->w0[w->window + n] == Catch::Approx(w0[n]));
        CHECK(w->w1[w->window + n] == Catch::Approx(w1[n]).margin(1e-12));
        CHECK(w->xhat[w->window + n] == Catch::Approx(kX[n]));
    }
    CHECK(w->u0.size() == 8);
    CHECK(w->u0[2] * r == Catch::Approx(22));
    CHECK(w->u0[1] == 0.0);
    CHECK(w->max_error < 1e-12);
    CHECK(w->delay == 1);
    CHECK(w->energy_x == Catch::Approx(446));
    CHECK(w->energy_a == Catch::Approx(440));
    CHECK(w->energy_d == Catch::Approx(6));
    REQUIRE(w->tree.size() == 4);
    CHECK(w->tree[0][0] == Catch::Approx(28 / r));
    CHECK(w->tree[1][0] == Catch::Approx(-4 / r));
    CHECK(w->tree[2][0] == Catch::Approx(6));
    CHECK(w->tree[2][1] == Catch::Approx(-2));
    CHECK(w->a1.rows() == 8);
    CHECK(w->max_alias_db < -250);
}

TEST_CASE("even phase keeps five plus five and delays by one", "[tui][explain]") {
    auto w = walkthrough(kX, {.odd_phase = false});
    REQUIRE(w);
    CHECK(w->kept == std::vector<std::size_t>{0, 2, 4, 6, 8});
    CHECK(w->a.size() == 5);
    CHECK(w->window == 1);
    CHECK(w->max_error < 1e-12);
}

TEST_CASE("db2 and 5/3 reconstruct in both phases", "[tui][explain]") {
    for (auto bank : {ExplainBank::db2, ExplainBank::legall53})
        for (bool odd : {true, false}) {
            auto w = walkthrough(kX, {.bank = bank, .odd_phase = odd});
            REQUIRE(w);
            INFO(w->bank_name << (odd ? " odd" : " even"));
            CHECK(w->max_error < 1e-12);
            CHECK(w->y0.size() == 8 + w->bank.length() - 1);
        }
}

TEST_CASE("perturbing f1 breaks alias cancellation", "[tui][explain]") {
    auto w = walkthrough(kX, {.perturb_f1 = true});
    REQUIRE(w);
    CHECK(w->max_alias_db > -60);
    CHECK(w->max_error > 1e-4);
    CHECK_FALSE(check_invariant("reconstruction", *w).passed);
    CHECK_FALSE(check_invariant("alias_cancelled", *w).passed);
}

TEST_CASE("zeroing details leaves the Haar alias at -6 dB", "[tui][explain]") {
    auto w = walkthrough(kX, {.zero_detail = true});
    REQUIRE(w);
    CHECK(w->branch_alias_db == Catch::Approx(-6.0206).margin(0.01));
    CHECK(w->max_alias_db < -250);
    CHECK(check_invariant("alias_cancelled", *w).passed);
    const auto line = fill("максимум {{alias_db}} дБ", *w);
    REQUIRE(line);
    CHECK(*line == "максимум −6,02 дБ");
    for (std::size_t n = 0; n < 8; ++n) CHECK(w->xhat[w->window + n] == Catch::Approx(w->w0[w->window + n]));
    CHECK(check_invariant("zero_detail_error", *w).passed);
}

TEST_CASE("summing normalisation prints exact fractions", "[tui][explain]") {
    auto w = walkthrough(kX, {.summing = true});
    REQUIRE(w);
    CHECK(w->max_error < 1e-12);
    CHECK(format_cell(w->a[0], true) == "5");
    CHECK(format_cell(w->y0[0], true) == "2");
    CHECK(format_cell(w->d[2], true) == "−1");
    CHECK(format_cell(0.375, true) == "3/8");
    CHECK(format_cell(std::sqrt(2.0), true) == "1,414");
    CHECK(format_cell(7.0710678, false) == "7,071");
    CHECK(format_cell(-1.5, false) == "−1,5");
}

TEST_CASE("the haar script has twelve canvas steps and every placeholder resolves", "[tui][explain]") {
    const auto& s = haar_script();
    REQUIRE(s.size() == 12);
    CHECK(s[0].title == "сигнал");
    CHECK(s[3].title == "прореживание ↓2");
    CHECK(s[11].title == "эксперимент: d = 0");
    for (auto bank : {ExplainBank::haar, ExplainBank::db2, ExplainBank::legall53})
        for (bool odd : {true, false}) {
            auto w = walkthrough(kX, {.bank = bank, .odd_phase = odd});
            REQUIRE(w);
            for (const auto& step : s)
                for (const auto* lines : {&step.text, &step.text_haar})
                    for (const auto& line : *lines) {
                        INFO(step.title << ": " << line);
                        CHECK(fill(line, *w));
                    }
        }
    auto w = walkthrough(kX, {});
    REQUIRE(w);
    CHECK(*fill("инвариант: {{x.energy}} = {{a.energy}} + {{d.energy}}", *w) == "инвариант: 446 = 440 + 6");
    CHECK(*fill("{{kept}} · {{tree.energy}}", *w) == "1, 3, 5, 7 · 392 + 8 + 40 + 6 = 446");
    CHECK_FALSE(fill("{{nonsense}}", *w));
}

TEST_CASE("the English haar script has the same twelve steps", "[tui][explain][i18n]") {
    const signals::LanguageScope en{signals::Language::en};
    const auto& s = haar_script();
    REQUIRE(s.size() == 12);
    CHECK(s[0].title == "signal");
    CHECK(s[3].title == "downsampling ↓2");
    auto w = walkthrough(kX, {});
    REQUIRE(w);
    for (const auto& step : s)
        for (const auto* lines : {&step.text, &step.text_haar})
            for (const auto& line : *lines) {
                INFO(step.title << ": " << line);
                CHECK(fill(line, *w));
            }
}

TEST_CASE("a script with a broken step is an error", "[tui][explain]") {
    CHECK_FALSE(load_script("[[step]]\ntitle = 3\n"));
    CHECK_FALSE(load_script("not toml ["));
}

TEST_CASE("invariants are honest for a biorthogonal bank", "[tui][explain]") {
    auto w = walkthrough(kX, {.bank = ExplainBank::legall53});
    REQUIRE(w);
    CHECK_FALSE(check_invariant("parseval", *w).passed);
    CHECK_FALSE(check_invariant("power_complementary", *w).passed);
    CHECK(check_invariant("reconstruction", *w).passed);
    CHECK(check_invariant("orthogonal_matrix", *w).passed);
    CHECK(check_invariant("energy", *w).passed);
}

TEST_CASE("a user bank too long for N = 8 still explains what it can", "[tui][explain]") {
    auto bank = signals::core::wavelet("db10");
    REQUIRE(bank);
    auto w = walkthrough(kX, {.bank = ExplainBank::user, .user = *bank});
    REQUIRE(w);
    CHECK(w->max_error < 1e-12);
    CHECK(w->tree.empty());
    CHECK_FALSE(check_invariant("tree_energy", *w).passed);
    CHECK(*fill("{{a2}}", *w) == "—");
}

TEST_CASE("fractions mode keeps power complementarity and the matrix identity honest", "[tui][explain]") {
    auto w = walkthrough(kX, {.summing = true});
    REQUIRE(w);
    CHECK(check_invariant("power_complementary", *w).passed);
    const auto& step9 = haar_script()[8];
    const auto line = fill(step9.text_haar.back(), *w);
    REQUIRE(line);
    CHECK(line->find("S·A₁ = I") != std::string::npos);
    CHECK(check_invariant("orthogonal_matrix", *w).passed);
    const auto gain = fill(haar_script()[1].text.back(), *w);
    REQUIRE(gain);
    CHECK(gain->ends_with("= 1"));
}

TEST_CASE("facts are not reported as checks", "[tui][explain]") {
    auto w = walkthrough(kX, {});
    REQUIRE(w);
    for (const char* id : {"energy", "conv_length", "coefficient_count", "upsampled_length"}) CHECK_FALSE(check_invariant(id, *w).checked);
    CHECK(check_invariant("reconstruction", *w).checked);
}
