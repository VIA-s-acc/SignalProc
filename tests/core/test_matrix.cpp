#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/analysis.hpp"
#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/matrix.hpp"
#include "support/banks.hpp"
#include "support/golden.hpp"

using namespace signals::core;

TEST_CASE("toeplitz matrix times x equals convolution", "[matrix]") {
    Laurent h{{0.5, -1, 2}, -1};
    std::vector<double> x{1, 2, 3, 4, 5};
    Eigen::VectorXd y = convolution_matrix(h, 5) * Eigen::Map<Eigen::VectorXd>(x.data(), 5);
    auto ref = convolve(Laurent{x, 0}, h);
    REQUIRE(static_cast<std::size_t>(y.size()) == ref.coeffs.size());
    for (int i = 0; i < y.size(); ++i) CHECK(y[i] == Catch::Approx(ref.coeffs[static_cast<std::size_t>(i)]));
}

TEST_CASE("circulant is diagonalised by the dft", "[matrix]") {
    const std::size_t n = 12;
    Laurent h{{0.3, -1.2, 0.7, 2.0}, -2};
    const Eigen::MatrixXcd c = circulant_matrix(h, n).cast<std::complex<double>>();
    for (std::size_t k = 0; k < n; ++k) {
        const double w = 2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(n);
        Eigen::VectorXcd v(static_cast<Eigen::Index>(n));
        for (std::size_t m = 0; m < n; ++m) v[static_cast<Eigen::Index>(m)] = std::polar(1.0, w * static_cast<double>(m));
        CHECK((c * v - evaluate(h, w) * v).cwiseAbs().maxCoeff() < 1e-12);
    }
}

TEST_CASE("fft circulant product equals dense product", "[matrix]") {
    std::mt19937_64 rng(3);
    std::normal_distribution<double> g;
    Laurent h{{}, 4};
    for (int i = 0; i < 9; ++i) h.coeffs.push_back(g(rng));
    std::vector<double> x(37);
    for (auto& v : x) v = g(rng);
    Eigen::VectorXd dense = circulant_matrix(h, 37) * Eigen::Map<Eigen::VectorXd>(x.data(), 37);
    auto fast = circulant_multiply_fft(h, x);
    for (int i = 0; i < 37; ++i) CHECK(std::abs(dense[i] - fast[static_cast<std::size_t>(i)]) < 1e-12);
}

TEST_CASE("haar three-level matrix rows are the discrete haar functions", "[matrix]") {
    auto w = *multilevel_matrix(*wavelet("haar"), 8, 3, {});
    const double s8 = 1 / std::sqrt(8.0);
    CHECK(w.row(0).isApprox(Eigen::RowVectorXd::Constant(8, s8)));
    Eigen::RowVectorXd d3(8);
    d3 << -s8, -s8, -s8, -s8, s8, s8, s8, s8;
    CHECK(w.row(1).isApprox(d3));
    CHECK(check_orthogonal_matrix(w, 1e-14).passed());
}

TEST_CASE("synthesis inverts analysis in every mode", "[matrix]") {
    const auto b = *wavelet("db2");
    for (auto m : signals::test::all_modes)
        for (std::size_t n : {16, 17}) {
            INFO(to_string(m) << " n=" << n);
            CHECK(check_left_inverse(*synthesis_matrix(b, n, {m}), *analysis_matrix(b, n, {m}), 1e-13).passed());
        }
}

TEST_CASE("db3 periodization matrix is orthogonal", "[matrix]") {
    CHECK(check_orthogonal_matrix(*analysis_matrix(*wavelet("db3"), 32, {Extension::periodization}), 1e-14).passed());
}

TEST_CASE("matrices of invalid transforms are errors", "[matrix]") {
    const auto b = *wavelet("db2");
    auto a = analysis_matrix(b, 1, {Extension::reflect});
    REQUIRE_FALSE(a);
    CHECK(a.error().code == Errc::invalid_argument);
    auto w = multilevel_matrix(b, 3, 1, {});
    REQUIRE_FALSE(w);
    CHECK(w.error().code == Errc::level_out_of_range);
}

TEST_CASE("matrix checks fail on empty or mismatched matrices", "[matrix]") {
    CHECK_FALSE(check_orthogonal_matrix(Eigen::MatrixXd{}, 1.0).passed());
    CHECK_FALSE(check_left_inverse(Eigen::MatrixXd::Identity(3, 3), Eigen::MatrixXd::Identity(4, 4), 1.0).passed());
    CHECK(circulant_matrix(Laurent{{1, 2}, 0}, 0).size() == 0);
}

TEST_CASE("nan in a matrix never passes a matrix check", "[matrix]") {
    Eigen::MatrixXd a = Eigen::MatrixXd::Identity(8, 8);
    a(7, 7) = std::nan("");
    CHECK_FALSE(check_orthogonal_matrix(a, 1e-12).passed());
    CHECK_FALSE(check_left_inverse(Eigen::MatrixXd::Identity(8, 8), a, 1e-12).passed());
}

TEST_CASE("orthogonal analysis matrices have unit singular values", "[core][matrix]") {
    auto a = analysis_matrix(*wavelet("db2"), 32, {Extension::periodization});
    REQUIRE(a);
    auto s = singular_values(*a);
    REQUIRE(s.size() == 32);
    CHECK(std::abs(s.front() - 1) < 1e-12);
    CHECK(std::abs(s.back() - 1) < 1e-12);
    CHECK(condition_number(s) == Catch::Approx(1).margin(1e-12));
}

TEST_CASE("singular values are sorted and a singular matrix has infinite condition", "[core][matrix]") {
    Eigen::MatrixXd m(2, 2);
    m << 3, 0, 0, 0;
    auto s = singular_values(m);
    CHECK(s == std::vector<double>{3, 0});
    CHECK(std::isinf(condition_number(s)));
    CHECK(std::isinf(condition_number(std::vector<double>{})));
}

TEST_CASE("circulant times x equals FFT circular convolution", "[core][matrix]") {
    const auto bank = signals::core::wavelet("db4");
    REQUIRE(bank);
    auto r = signals::core::check_circulant_fft(bank->h0, 64, 1e-12);
    CHECK(r.id == "check.circulant_fft");
    CHECK(r.passed());
    CHECK(r.value < 1e-12);
}

TEST_CASE("a NaN filter fails the circulant check", "[core][matrix]") {
    signals::core::Laurent h{{1.0, std::numeric_limits<double>::quiet_NaN()}, 0};
    CHECK_FALSE(signals::core::check_circulant_fft(h, 16, 1e-12).passed());
}

TEST_CASE("the dct-ii matrix matches the fast transform and is orthogonal", "[matrix][toeplitz_hankel]") {
    const auto c = dct2_matrix(9);
    CHECK((c * c.transpose() - Eigen::MatrixXd::Identity(9, 9)).cwiseAbs().maxCoeff() < 1e-14);
    std::vector<double> x(9);
    for (std::size_t k = 0; k < x.size(); ++k) x[k] = std::sin(1.3 * static_cast<double>(k * k));
    const Eigen::VectorXd y = c * Eigen::Map<const Eigen::VectorXd>(x.data(), 9);
    const auto fast = dct2(x);
    for (std::size_t k = 0; k < x.size(); ++k) CHECK(std::abs(y[static_cast<Eigen::Index>(k)] - fast[k]) < 1e-14);
}

TEST_CASE("toeplitz plus hankel of a symmetric filter is diagonalised by the dct-ii", "[matrix][toeplitz_hankel]") {
    constexpr std::size_t n = 16;
    const auto c = dct2_matrix(n);
    for (const auto& h : {Laurent{{0.25, 0.5, 0.25}, -1}, Laurent{{-0.125, 0.25, 0.75, 0.25, -0.125}, -2}}) {
        const auto a = symmetric_convolution_matrix(h, n);
        const Eigen::MatrixXd d = c * a * c.transpose();
        for (Eigen::Index i = 0; i < d.rows(); ++i)
            for (Eigen::Index j = 0; j < d.cols(); ++j) {
                if (i != j) {
                    CHECK(std::abs(d(i, j)) < 1e-14);
                    continue;
                }
                const double w = std::numbers::pi * static_cast<double>(i) / static_cast<double>(n);
                double expected = 0;
                for (std::size_t k = 0; k < h.coeffs.size(); ++k) expected += h.coeffs[k] * std::cos(w * static_cast<double>(h.offset + static_cast<std::ptrdiff_t>(k)));
                CHECK(std::abs(d(i, i) - expected) < 1e-14);
            }
        CHECK(diagonalisation_error(a, c) < 1e-14);
    }
    const auto skew = symmetric_convolution_matrix(Laurent{{0.48296, 0.83652, 0.22414, -0.12941}, 0}, n);
    CHECK(diagonalisation_error(skew, c) > 1e-2);
    const Laurent box{{0.25, 0.5, 0.25}, -1};
    const auto a = symmetric_convolution_matrix(box, n);
    const auto t = convolution_matrix(box, n);
    CHECK(std::abs(a(5, 4) - 0.25) < 1e-15);
    CHECK(std::abs(a(0, 0) - 0.75) < 1e-15);
    CHECK(t.rows() == static_cast<Eigen::Index>(n + 2));
}

TEST_CASE("levinson-durbin matches scipy solve_toeplitz", "[matrix][levinson][golden]") {
    const auto g = signals::test::load_golden("ops.json");
    for (const auto& c : g["levinson"]) {
        const auto r = c["r"].get<std::vector<double>>();
        const auto b = c["b"].get<std::vector<double>>();
        const auto x = levinson_solve(r, b);
        REQUIRE(x);
        const auto expected = c["x"].get<std::vector<double>>();
        REQUIRE(x->x.size() == expected.size());
        for (std::size_t k = 0; k < expected.size(); ++k) CHECK(std::abs(x->x[k] - expected[k]) < 1e-10 * std::max(1.0, std::abs(expected[k])));
        CHECK(x->reflection.size() == r.size() - 1);
    }
    CHECK_FALSE(levinson_solve(std::vector<double>{0.0, 1.0}, std::vector<double>{1.0, 1.0}));
    CHECK_FALSE(levinson_solve(std::vector<double>{1.0, 1.0}, std::vector<double>{1.0, 2.0}));
    CHECK_FALSE(levinson_solve(std::vector<double>{1.0, 0.5}, std::vector<double>{1.0}));
}

TEST_CASE("Gohberg-Semencul inverse matches numpy on symmetric Toeplitz matrices", "[matrix][toeplitz][golden]") {
    const auto g = signals::test::load_golden("toeplitz.json");
    for (const auto& c : g["cases"]) {
        const auto r = c["r"].get<std::vector<double>>();
        const auto expected = c["inverse"].get<std::vector<std::vector<double>>>();
        const auto inv = toeplitz_inverse(r);
        REQUIRE(inv);
        REQUIRE(inv->rows() == static_cast<Eigen::Index>(r.size()));
        double scale = 0;
        for (const auto& row : expected)
            for (double v : row) scale = std::max(scale, std::abs(v));
        for (std::size_t i = 0; i < r.size(); ++i)
            for (std::size_t j = 0; j < r.size(); ++j) CHECK(std::abs((*inv)(i, j) - expected[i][j]) < 1e-10 * std::max(1.0, scale));
    }
    CHECK_FALSE(toeplitz_inverse(std::vector<double>{1.0, 1.0, 1.0}));
    CHECK_FALSE(toeplitz_inverse(std::vector<double>{}));
    const auto one = toeplitz_inverse(std::vector<double>{4.0});
    REQUIRE(one);
    CHECK((*one)(0, 0) == 0.25);
}
