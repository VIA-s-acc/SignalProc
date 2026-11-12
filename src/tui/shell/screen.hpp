#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "audio/player.hpp"
#include "theory/live.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/widgets/exportable.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/ticker.hpp"
#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"
#include "i18n/i18n.hpp"

namespace signals::tui {

struct Command {
    std::string id;
    std::string title;
    std::function<void()> run;
    std::string note = {};
};

struct NavArgs {
    std::string wavelet = {};
    std::optional<std::size_t> n = {};
    std::optional<std::size_t> step = {};
};

struct TheoryLink {
    enum class Kind { card, section, source };
    Kind kind = Kind::section;
    std::string id;
    std::string label = {};
};

struct TheoryPage {
    TheoryLink link;
    std::shared_ptr<theory::LiveValues> values = {};
    std::function<void(theory::LiveValues&)> publisher = {};
    std::string origin = {};
    int scroll = 0;
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
    std::filesystem::path user_filters;
    bool first_run_check = false;
    audio::Player* player = nullptr;
    Ticker* ticker = nullptr;
    std::function<std::chrono::steady_clock::time_point()> now;
    std::function<void(std::string_view target, const NavArgs& args)> navigate;
    std::function<void(std::string filter)> open_palette;
    std::function<void(std::string prompt, std::string initial, std::function<void(std::string)> done)> ask_text;
    std::function<void(std::string title, std::vector<Command> items)> choose;
    std::function<void(std::string line)> warn;
    std::function<void(std::filesystem::path)> open_in_editor;
    std::string editor;
    std::function<void(std::string)> copy_text;
    std::function<void(Language)> switch_language;
    std::filesystem::path repo_root;
    std::function<void(TheoryPage)> open_theory;
    std::function<void(std::string)> notice;
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
    virtual std::vector<PlotExport> plot_exports() { return {}; }
    virtual std::vector<HeatmapExport> heatmap_exports() { return {}; }
    virtual std::vector<JumpTarget> jump_targets() const { return {}; }
    virtual std::optional<std::string> explain_at(int /*x*/, int /*y*/) const { return std::nullopt; }
    virtual void open_with(const NavArgs&) {}
    virtual std::vector<Command> commands() { return {}; }
    virtual ftxui::Element context_element(const UiContext& ui, int width);
    virtual std::optional<std::pair<int, int>> menu_anchor() const { return std::nullopt; }
    virtual void select_child(std::size_t) {}
    virtual std::optional<TheoryLink> theory_link() const { return {}; }
    virtual void publish(theory::LiveValues&) const {}
    virtual std::shared_ptr<TabScreen> active_child() const { return nullptr; }
};

}  // namespace signals::tui
