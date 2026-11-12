#pragma once

#include <string>
#include <string_view>

namespace signals::theory {

struct BlockTheory {
    bool section = false;
    std::string id;
};

BlockTheory block_theory(std::string_view block);

}  // namespace signals::theory
