#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "learn/task.hpp"
#include "theory/markdown.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/document.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/scroll.hpp"

namespace signals::tui {

class LearnScreen final : public TabScreen {
public:
    enum Panel { list_panel, text_panel, goals_panel, hints_panel, panel_count };

    explicit LearnScreen(UiContext& ui);
    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    bool capturing_text() const override { return typing_; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    std::vector<Command> commands() override;
    std::optional<TheoryLink> theory_link() const override;
    std::vector<JumpTarget> jump_targets() const override;
    std::optional<std::string> explain_at(int x, int y) const override;
    void open_with(const NavArgs& args) override;

    Panel focus() const { return focus_; }
    const std::string& cursor() const { return cursor_; }

private:
    struct Row {
        const learn::Chapter* chapter = nullptr;
        const learn::Task* task = nullptr;
    };
    struct Texts {
        std::string key;
        std::vector<theory::Block> statement, hints;
        std::vector<std::string> goals;
    };
    struct Clickable {
        std::string id;
        ftxui::Box box;
        std::function<void()> action;
    };

    const learn::Task* task() const;
    std::vector<Row> rows() const;
    void move(int delta);
    void start();
    void hint();
    void solution();
    void answer();
    void submit();
    void open_theory() const;
    const Texts& texts();
    ftxui::Element clickable(std::string id, ftxui::Element e, std::function<void()> action);
    ftxui::Element frame(Panel p, std::string title, std::vector<Chip> chips, ftxui::Element body);
    ftxui::Element render_panel(Panel p, int width, int height);
    ftxui::Element list_body(int width, int height);
    ftxui::Element text_body(int width, int height);
    ftxui::Element goals_body(int width, int height);
    ftxui::Element hints_body(int width, int height);
    ftxui::Element document_body(const std::vector<theory::Block>& blocks, int width, int height, Panel p);
    std::string panel_title(Panel p) const;
    bool scroll(Panel p, int delta);

    UiContext& ui_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    Panel focus_ = list_panel;
    std::optional<Panel> maximized_;
    std::string cursor_;
    Scroll list_scroll_;
    std::array<int, panel_count> scroll_{};
    std::array<int, panel_count> doc_height_{};
    std::array<int, panel_count> view_height_{};
    std::array<ftxui::Box, panel_count> boxes_{};
    std::array<DocumentLayout, panel_count> layouts_{};
    std::deque<std::pair<ftxui::Box, std::string>> row_boxes_;
    std::deque<Clickable> clickables_;
    Notes notes_{"learn"};
    bool typing_ = false;
    std::string answer_;
    Texts texts_;
};

std::shared_ptr<TabScreen> make_learn_screen(UiContext& ui);

}  // namespace signals::tui
