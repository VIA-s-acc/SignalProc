#include "core/extension.hpp"

#include <array>
#include <utility>

namespace signals::core {
namespace {

constexpr std::array<std::pair<Extension, std::string_view>, 9> kNames{{
    {Extension::zero, "zero"},
    {Extension::constant, "constant"},
    {Extension::symmetric, "symmetric"},
    {Extension::reflect, "reflect"},
    {Extension::periodic, "periodic"},
    {Extension::smooth, "smooth"},
    {Extension::periodization, "periodization"},
    {Extension::antisymmetric, "antisymmetric"},
    {Extension::antireflect, "antireflect"},
}};

}  // namespace

std::optional<Extension> parse_extension(std::string_view pywt_name) {
    for (const auto& [mode, name] : kNames)
        if (name == pywt_name) return mode;
    return std::nullopt;
}

std::string_view to_string(Extension mode) {
    for (const auto& [m, name] : kNames)
        if (m == mode) return name;
    return "unknown";
}

std::size_t dwt_coeff_len(std::size_t n, std::size_t filter_len, Extension mode) {
    if (mode == Extension::periodization) return (n + 1) / 2;
    return (n + filter_len - 1) / 2;
}

}  // namespace signals::core
