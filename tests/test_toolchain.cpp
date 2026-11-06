#include <boost/multiprecision/cpp_bin_float.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <Eigen/Dense>

#include "core/scalar.hpp"
#include "core/version.hpp"

TEST_CASE("toolchain provides required libraries", "[toolchain]") {
    signals::core::Rational r{1, 3};
    CHECK(r * 3 == 1);
    using F256 = boost::multiprecision::number<
        boost::multiprecision::cpp_bin_float<256, boost::multiprecision::digit_base_2>>;
    CHECK(abs(sqrt(F256{2}) * sqrt(F256{2}) - 2) < F256{"1e-70"});
    Eigen::Matrix2d m;
    m << 2, 0, 0, 3;
    CHECK(m.determinant() == Catch::Approx(6.0));
    CHECK(signals::core::version() == SIGNALS_EXPECTED_VERSION);
}
