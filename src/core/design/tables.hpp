#pragma once

#include <string_view>
#include <vector>

namespace signals::core {

struct TableEntry {
    std::string_view name;
    std::vector<double> dec_lo;
    std::vector<double> rec_lo;
};

const TableEntry* find_table(std::string_view name);

}  // namespace signals::core
