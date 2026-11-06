#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>

#include "theory/markdown.hpp"
#include "theory/math_layout.hpp"
#include "tui/screens/theory_navigator.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/screen.hpp"
#include "tui/widgets/document.hpp"

namespace signals::tui {

class TheoryScreen final : public TabScreen {
public:
    enum class Mode { tutorial, under_the_hood };

    TheoryScreen(UiContext& ui, std::shared_ptr<TheoryNavigator> nav, Mode mode);

    std::string tab_label() const override;
    std::vector<Hint> hints() const override;
    std::string context() const override;
    void set_breakpoint(Breakpoint b) override { breakpoint_ = b; }
    bool capturing_text() const override { return searching_; }
    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event e) override;
    std::vector<JumpTarget> jump_targets() const override;

    ftxui::Box area(std::string_view id) const;
    std::optional<std::size_t> focused_link() const { return focused_link_; }
    const std::string& query() const { return query_; }

private:
    struct Entry {
        std::size_t section;
        int depth;
    };

    const TheoryPage* page() const;
    std::size_t current_section() const;
    void open_section(std::string_view id);
    void follow(const std::string& target);
    const std::vector<theory::Block>& blocks_for(std::size_t section);
    std::vector<Entry> contents(std::size_t current) const;
    ftxui::Element contents_panel(std::size_t current, int width, int height);
    ftxui::Element text_panel(std::size_t current, int width, int height);
    void sync_version();
    bool on_mouse(ftxui::Mouse m);

    UiContext& ui_;
    std::shared_ptr<TheoryNavigator> nav_;
    Mode mode_;
    Breakpoint breakpoint_ = Breakpoint::primary;
    theory::MathGlyphs glyphs_;
    std::string query_;
    bool searching_ = false;
    std::vector<std::size_t> hits_;
    std::size_t selected_hit_ = 0;
    std::optional<std::size_t> focused_link_;
    int scroll_ = 0, hscroll_ = 0, visible_rows_ = 20;
    bool small_contents_ = false;
    std::size_t seen_version_ = static_cast<std::size_t>(-1);
    DocumentLayout doc_;
    ftxui::Box doc_box_{}, references_box_{};
    std::vector<std::pair<ftxui::Box, std::size_t>> toc_boxes_;
    std::map<std::size_t, std::vector<theory::Block>> cache_;
};

std::shared_ptr<TheoryScreen> make_theory_screen(UiContext& ui, std::shared_ptr<TheoryNavigator> nav, TheoryScreen::Mode mode);

}  // namespace signals::tui
