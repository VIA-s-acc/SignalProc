#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/bank.hpp"
#include "core/design/biorthogonal.hpp"

namespace signals::core {

struct WaveletInfo {
    std::string name;
    std::string family;
    bool orthogonal = false;
    bool biorthogonal = false;
    bool generated = false;
    std::optional<int> vanishing_moments_psi;
};

Result<FilterBank> wavelet(std::string_view name);
std::span<const std::string_view> wavelet_names();
Result<WaveletInfo> wavelet_info(std::string_view name);

}  // namespace signals::core
