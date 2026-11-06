#pragma once

#include <cstddef>
#include <vector>

#include "tui/shell/screen.hpp"

namespace signals::tui {

class TheoryNavigator {
public:
    void open(TheoryPage page);
    bool back();
    bool forward();
    const TheoryPage* current() const;
    TheoryPage* current();
    const TheoryPage* last(TheoryLink::Kind kind) const;
    TheoryPage* last(TheoryLink::Kind kind);
    std::size_t version() const { return version_; }

private:
    std::vector<TheoryPage> history_;
    std::size_t index_ = 0;
    std::size_t version_ = 0;
};

}  // namespace signals::tui
