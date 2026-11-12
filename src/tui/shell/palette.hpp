#pragma once

#include <cstddef>
#include <functional>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "tui/shell/screen.hpp"
#include "tui/term/caps.hpp"

namespace signals::tui {


std::vector<const Command*> filter_commands(const std::vector<Command>& all, std::string_view query);

class Palette {
public:
    void open(std::vector<Command> commands, std::string filter = {}, std::string title = {});
    void ask_path(std::string prompt, std::function<void(std::string)> done);
    void ask_text(std::string prompt, std::string initial, std::function<void(std::string)> done);
    void close();
    bool is_open() const { return open_; }
    bool on_event(const ftxui::Event& e);
    ftxui::Element render(const UiContext& ui, int width) const;

private:
    std::vector<Command> commands_;
    std::string query_;
    bool prefilled_ = false;
    void select(std::size_t index, std::size_t count);

    std::size_t selected_ = 0;
    std::size_t first_ = 0;
    bool open_ = false;
    std::string prompt_, title_;
    std::function<void(std::string)> path_done_;
};

void pop_codepoint(std::string& s);
std::filesystem::path expand_path(std::string text, const EnvLookup& env = process_env());

}  // namespace signals::tui
