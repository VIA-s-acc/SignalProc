#pragma once

#include <span>
#include <string_view>

namespace signals::theory {

enum class Tier { core, v1, v2, research };

struct BlockSpec {
    std::string_view id, title, section;
    Tier tier;
    std::span<const std::string_view> values;
};

std::span<const BlockSpec> block_catalog();
const BlockSpec* find_block(std::string_view id);

}  // namespace signals::theory
