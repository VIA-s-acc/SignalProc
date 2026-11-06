#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "audio/player.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/ticker.hpp"
#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

struct Command {
    std::string id;
    std::string title;
    std::function<void()> run;
};

struct NavArgs {
    std::string wavelet = {};
    std::optional<std::size_t> n = {};
    std::optional<std::size_t> step = {};
};

struct UiContext {
    Theme theme;
    TerminalCaps caps;
    JobRunner* jobs = nullptr;
    ResourceMonitor* monitor = nullptr;
    std::function<ResourceSnapshot()> resources;
    std::function<void(std::filesystem::path)> open_file;
    std::function<void()> ask_open;
    std::function<std::pair<int, int>()> terminal_size;
    std::function<std::vector<ResourceSnapshot>()> history;
    std::filesystem::path prefs_file;
    bool first_run_check = false;
    audio::Player* player = nullptr;
    Ticker* ticker = nullptr;
    std::function<std::chrono::steady_clock::time_point()> now;
    std::function<void(std::string_view target, const NavArgs& args)> navigate;
    std::function<void(std::string filter)> open_palette;
    std::function<void(std::string prompt, std::string initial, std::function<void(std::string)> done)> ask_text;
};

struct MenuItem {
    std::string label;
    std::string key = {};
    std::function<void()> action = {};
    bool enabled = true;
};

struct JumpTarget {
    ftxui::Box box;
    std::string label;
};

struct Hint {
    std::string key;
    std::string label;
};

class TabScreen : public ftxui::ComponentBase {
public:
    virtual std::string tab_label() const = 0;
    virtual std::vector<Hint> hints() const = 0;
    virtual std::string context() const { return {}; }
    virtual void set_breakpoint(Breakpoint b) = 0;
    virtual bool capturing_text() const { return false; }
    virtual std::vector<MenuItem> context_menu(int /*x*/, int /*y*/) { return {}; }
    virtual std::vector<JumpTarget> jump_targets() const { return {}; }
    virtual void open_with(const NavArgs&) {}
    virtual std::vector<Command> commands() { return {}; }
    virtual ftxui::Element context_element(const UiContext& ui);
    virtual void select_child(std::size_t) {}
};

}  // namespace signals::tui
