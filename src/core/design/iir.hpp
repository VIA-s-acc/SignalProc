#pragma once

#include <array>
#include <complex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace signals::core {

enum class IirFamily { butterworth, chebyshev1, chebyshev2, elliptic, bessel };
enum class BandType { lowpass, highpass, bandpass, bandstop };

struct Zpk {
    std::vector<std::complex<double>> z, p;
    double k = 1;
};

struct IirSpec {
    IirFamily family = IirFamily::butterworth;
    BandType type = BandType::lowpass;
    int order = 4;
    std::vector<double> wn = {0.25};
    double rp = 1, rs = 40;
};

using Sos = std::array<double, 6>;

Result<Zpk> analog_prototype(IirFamily family, int order, double rp, double rs);
Result<Zpk> iir_design(const IirSpec& spec);
std::vector<Sos> zpk_to_sos(const Zpk& zpk);
std::vector<std::complex<double>> zpk_response(const Zpk& zpk, std::span<const double> w);
std::vector<std::complex<double>> sos_response(std::span<const Sos> sos, std::span<const double> w);
bool is_stable(const Zpk& zpk);
std::optional<IirFamily> parse_iir_family(std::string_view name);
std::optional<BandType> parse_band_type(std::string_view name);

}  // namespace signals::core
