#include "core/design/tables.hpp"

namespace signals::core {
namespace {

const std::vector<TableEntry>& tables() {
    static const std::vector<TableEntry> entries{
#include "core/design/wavelet_tables.inc"
    };
    return entries;
}

}  // namespace

const TableEntry* find_table(std::string_view name) {
    for (const auto& e : tables())
        if (e.name == name) return &e;
    return nullptr;
}

}  // namespace signals::core
