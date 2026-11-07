#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/bank.hpp"
#include "core/error.hpp"

namespace signals::tui {

struct Wire {
    std::string id;
    std::vector<double> values;
};

struct DiagramResult {
    std::string bank;
    std::vector<Wire> direct, polyphase;
    std::ptrdiff_t delay = 0;
    double error = 0;
    double direct_mults = 0, polyphase_mults = 0;

    const Wire* wire(std::string_view id, bool polyphase_view = false) const;
};

core::Result<DiagramResult> run_diagram(const core::FilterBank& bank, std::span<const double> x);

}  // namespace signals::tui
