#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace signals::theory {

struct Section {
    std::string id, number, title, tier;
    int level = 0;
    std::string body;
    std::optional<std::size_t> parent;
};

const std::vector<Section>& sections();
const Section* find_section(std::string_view id);
const Section* find_section_by_number(std::string_view number);
std::vector<std::size_t> search_sections(std::string_view query);
std::string_view own_body(const Section& s);
std::vector<std::size_t> children_of(std::size_t index);
std::size_t index_of(const Section& s);

}  // namespace signals::theory
