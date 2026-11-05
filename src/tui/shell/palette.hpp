#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "tui/shell/screen.hpp"

namespace signals::tui {

struct Command {
    std::string id;
    std::string title;
    std::function<void()> run;
};

std::vector<const Command*> filter_commands(const std::vector<Command>& all, std::string_view query);

class Palette {
public:
    void open(std::vector<Command> commands);
    void ask_path(std::string prompt, std::function<void(std::string)> done);
    void close();
    bool is_open() const { return open_; }
    bool on_event(const ftxui::Event& e);
    ftxui::Element render(const UiContext& ui) const;

private:
    std::vector<Command> commands_;
    std::string query_;
    std::size_t selected_ = 0;
    bool open_ = false;
    std::string prompt_;
    std::function<void(std::string)> path_done_;
};

void pop_codepoint(std::string& s);

}  // namespace signals::tui
