#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/bank.hpp"
#include "core/dwt.hpp"
#include "core/wavedec.hpp"
#include "core/extension.hpp"

namespace signals::pipeline {

enum class PortType { signal, coeffs, filter, scalar, report };

struct Coeffs {
    std::vector<core::BasicCoeffs<double>> channels;
    std::string wavelet;
    core::Extension mode = core::Extension::symmetric;
    double rate = 0;
};

struct Report {
    std::vector<std::pair<std::string, std::variant<double, std::string>>> fields;
    bool passed = true;
};

using Value = std::variant<core::AudioBuffer, Coeffs, core::FilterBank, double, Report>;

PortType type_of(const Value& v);
std::string_view to_string(PortType t);
std::uint64_t value_key(const Value& v);
std::size_t value_bytes(const Value& v);

}  // namespace signals::pipeline
