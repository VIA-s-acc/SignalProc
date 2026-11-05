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

struct Regularity {
    std::optional<int> zeros_h0, zeros_f0;
    std::optional<double> sobolev_h0, sobolev_f0;
};

Result<FilterBank> wavelet(std::string_view name);
Result<Regularity> wavelet_regularity(std::string_view name);
std::span<const std::string_view> wavelet_names();
Result<WaveletInfo> wavelet_info(std::string_view name);

}  // namespace signals::core
