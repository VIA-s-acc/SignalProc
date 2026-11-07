#include "core/swt.hpp"

namespace signals::core {

std::size_t swt_max_level(std::size_t n) {
    std::size_t j = 0;
    while (n > 0 && n % 2 == 0) {
        n /= 2;
        ++j;
    }
    return j;
}

}  // namespace signals::core
