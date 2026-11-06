#include "tui/widgets/list_window.hpp"

#include <algorithm>

namespace signals::tui {

ListWindow list_window(std::size_t& first, std::size_t n, int budget, int row_height, std::optional<std::size_t> follow) {
    if (n == 0) return {};
    const auto fit = [&](std::size_t f) {
        int left = budget - (f > 0 ? 1 : 0);
        auto k = static_cast<std::size_t>(std::max(1, left / row_height));
        if (f + k < n) {
            left -= 1;
            k = static_cast<std::size_t>(std::max(1, left / row_height));
        }
        return std::min(k, n - std::min(f, n));
    };
    if (follow) {
        const auto f = std::min(*follow, n - 1);
        if (f < first) first = f;
        while (f >= first + fit(first)) ++first;
    }
    std::size_t last = 0;
    while (last + fit(last) < n) ++last;
    first = std::min(first, last);
    return {first, fit(first)};
}

}  // namespace signals::tui
