#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/shell/screen.hpp"

namespace signals::tui {

std::vector<std::string> jump_labels(std::size_t count);

class ContextMenu {
public:
    void open(std::vector<MenuItem> items, int x, int y);
    void close() { open_ = false; }
    bool is_open() const { return open_; }
    ftxui::Element render(const UiContext& ui, int width, int height);
    bool on_event(ftxui::Event e);

private:
    void activate(std::size_t index);
    void move(int direction);

    std::vector<MenuItem> items_;
    int x_ = 0, y_ = 0;
    std::size_t selected_ = 0;
    ftxui::Box frame_{};
    std::vector<ftxui::Box> rows_;
    bool open_ = false;
};

class JumpMode {
public:
    void start(std::vector<JumpTarget> targets);
    void cancel();
    bool is_active() const { return active_; }
    ftxui::Element render(const UiContext& ui) const;
    std::optional<ftxui::Box> on_key(std::string_view ch);

private:
    std::vector<JumpTarget> targets_;
    std::vector<std::string> labels_;
    std::string typed_;
    bool active_ = false;
};

}  // namespace signals::tui
