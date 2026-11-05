#pragma once

#include <boost/multiprecision/cpp_bin_float.hpp>
#include <boost/multiprecision/cpp_int.hpp>

namespace signals::core {

using Rational = boost::multiprecision::cpp_rational;
using Float256 = boost::multiprecision::number<
    boost::multiprecision::cpp_bin_float<256, boost::multiprecision::digit_base_2>>;

}  // namespace signals::core
