#pragma once

#include <array>

#include "core/bank.hpp"

namespace signals::core {

// Orders whose pywt table the root-phase linearity criterion does not reproduce; they use the table.
inline constexpr std::array<int, 7> kSymletTableFallback{11, 14, 15, 16, 18, 19, 20};

Result<FilterBank> daubechies(int n);
Result<FilterBank> symlet(int n);

}  // namespace signals::core
