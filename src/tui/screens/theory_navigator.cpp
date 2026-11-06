#include "tui/screens/theory_navigator.hpp"

#include <utility>

namespace signals::tui {
namespace {

constexpr std::size_t kHistory = 100;

}  // namespace

void TheoryNavigator::open(TheoryPage page) {
    if (!history_.empty()) history_.resize(index_ + 1);
    history_.push_back(std::move(page));
    if (history_.size() > kHistory) history_.erase(history_.begin());
    index_ = history_.size() - 1;
    ++version_;
}

bool TheoryNavigator::back() {
    if (history_.empty() || index_ == 0) return false;
    --index_;
    ++version_;
    return true;
}

bool TheoryNavigator::forward() {
    if (index_ + 1 >= history_.size()) return false;
    ++index_;
    ++version_;
    return true;
}

const TheoryPage* TheoryNavigator::current() const { return history_.empty() ? nullptr : &history_[index_]; }

TheoryPage* TheoryNavigator::current() { return history_.empty() ? nullptr : &history_[index_]; }

const TheoryPage* TheoryNavigator::last(TheoryLink::Kind kind) const {
    if (history_.empty()) return nullptr;
    for (std::size_t i = index_ + 1; i-- > 0;)
        if (history_[i].link.kind == kind) return &history_[i];
    return nullptr;
}

TheoryPage* TheoryNavigator::last(TheoryLink::Kind kind) {
    return const_cast<TheoryPage*>(static_cast<const TheoryNavigator*>(this)->last(kind));
}

}  // namespace signals::tui
