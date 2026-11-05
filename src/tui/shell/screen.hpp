#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component_base.hpp>

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
    std::function<std::pair<int, int>()> terminal_size;
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
};

}  // namespace signals::tui
