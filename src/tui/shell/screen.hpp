#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/shell/layout.hpp"
#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

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
};

}  // namespace signals::tui
