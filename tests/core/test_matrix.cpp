#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/matrix.hpp"
#include "support/banks.hpp"

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
