#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/audio_buffer.hpp"
#include "core/bank.hpp"
#include "core/dwt.hpp"
#include "core/wavedec.hpp"
#include "core/extension.hpp"
#include "core/packets.hpp"

namespace signals::pipeline {

enum class PortType { signal, coeffs, filter, scalar, report };

struct Coeffs {
    std::vector<core::BasicCoeffs<double>> channels;
    std::vector<std::size_t> lengths;
    std::string wavelet;
    core::Extension mode = core::Extension::symmetric;
    double rate = 0;
    std::string transform = "dwt";
    bool norm = false;
    std::vector<core::PacketNode> basis;
    std::size_t levels() const { return channels.empty() ? 0 : channels.front().bands.size() - 1; }
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
std::optional<std::size_t> band_index(const Coeffs& c, std::string_view name);
std::optional<Value> band_port(const Coeffs& c, std::string_view name);
std::string band_label(const Coeffs& c, std::size_t band);

}  // namespace signals::pipeline
