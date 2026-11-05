#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/bank.hpp"
#include "core/extension.hpp"

namespace signals::test {

inline constexpr std::array<core::Extension, 9> all_modes{
    core::Extension::zero,     core::Extension::constant,      core::Extension::symmetric,
    core::Extension::reflect,  core::Extension::periodic,      core::Extension::smooth,
    core::Extension::periodization, core::Extension::antisymmetric, core::Extension::antireflect};

inline const std::array<std::string_view, 9> pr_representative{"haar", "db2",     "db4",     "db10",   "sym5",
                                                                "coif3", "bior2.2", "bior4.4", "rbio3.1"};

const nlohmann::json& golden_wavelet(std::string_view name);
core::FilterBank golden_bank(std::string_view name);
std::vector<double> signal_of_length(std::size_t n);
double max_abs(std::span<const double> x);
double round_trip_error(const std::vector<double>& x, std::uint8_t wavelet_index, std::uint8_t mode_index);

}  // namespace signals::test
