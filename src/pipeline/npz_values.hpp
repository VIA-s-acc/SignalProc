#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/bank.hpp"
#include "core/error.hpp"
#include "io/npy.hpp"
#include "pipeline/value.hpp"

namespace signals::pipeline {

struct CoeffsHints {
    std::string wavelet, mode;
    std::optional<double> rate;
};

core::Result<io::NpzEntries> coeffs_entries(const Coeffs& c);
io::NpzEntries report_entries(const Report& r);
io::NpzEntries bank_entries(const core::FilterBank& b);
core::Result<io::NpzEntries> value_entries(const Value& v);
io::NpzEntries prefixed(std::string_view prefix, io::NpzEntries entries);
core::Result<Coeffs> coeffs_from_entries(const io::NpzEntries& entries, const CoeffsHints& hints);
std::vector<std::string> band_keys(const Coeffs& c);

}  // namespace signals::pipeline
