#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace signals::tui {

enum class Breakpoint { tiny, small, medium, primary, large, xlarge };

Breakpoint classify(int cols, int rows);

class PanelLayout {
public:
    struct Size {
        std::string key;
        std::array<int, 5> defaults;
    };

    explicit PanelLayout(std::vector<Size> sizes = {});

    void resize(int cols, int rows);
    Breakpoint breakpoint() const { return breakpoint_; }
    int& size(std::string_view key);

    void toggle_maximize(const std::string& panel);
    const std::optional<std::string>& maximized() const { return maximized_; }

    void next_panel(std::size_t count, bool back = false);
    std::size_t small_index() const { return small_index_; }

private:
    void apply_defaults();

    std::vector<Size> defaults_;
    std::map<std::string, int, std::less<>> sizes_;
    std::pair<int, int> dims_{-1, -1};
    Breakpoint breakpoint_ = Breakpoint::primary;
    std::optional<std::string> maximized_;
    std::size_t small_index_ = 0;
};

}  // namespace signals::tui
