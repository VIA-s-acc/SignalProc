#include "core/wavedec.hpp"

namespace signals::core {

std::size_t dwt_max_level(std::size_t n, std::size_t filter_len) {
    if (filter_len < 2 || n < filter_len - 1) return 0;
    std::size_t level = 0;
    while ((filter_len - 1) << (level + 1) <= n) ++level;
    return level;
}

}  // namespace signals::core
