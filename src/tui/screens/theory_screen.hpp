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

#include "theory/cards.hpp"
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
    std::optional<std::string> explain_at(int x, int y) const override;
    std::vector<MenuItem> context_menu(int x, int y) override;

    ftxui::Box area(std::string_view id) const;
    std::optional<std::size_t> focused_link() const { return focused_link_; }
    std::string focused_target() const { return focused_link_ && *focused_link_ < doc_.links.size() ? doc_.links[*focused_link_].target : std::string{}; }
    const std::string& query() const { return query_; }
    int scroll() const { return scroll_; }

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
    TheoryPage* card_page();
    void refresh_values(TheoryPage& page);
    std::string card_markdown(const theory::Card& card, bool live) const;
    ftxui::Element origin_line(const TheoryPage* page, int width) const;
    ftxui::Element blocks_list(const theory::Card* current, int width, int rows, std::optional<std::size_t> cursor);
    ftxui::Element card_panel(const TheoryPage* page, int width, int height);
    NavArgs view_args(const TheoryPage* page) const;
    void open_source();
    void clamp_scroll();
    bool open_section_card();
    std::vector<Entry> toc_entries() const;
    bool contents_keys() const;
    void start_cursor();
    bool activate_section(std::size_t index);
    bool activate_block(const std::string& id);
    bool contents_key(ftxui::Event e);
    ftxui::Element more_row(bool up, std::size_t count, int width, ftxui::Box& box) const;

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
    std::vector<theory::Block> card_blocks_;
    std::vector<std::pair<ftxui::Box, std::string>> block_boxes_;
    ftxui::Box blocks_box_{}, blocks_up_box_{}, blocks_down_box_{}, card_link_box_{};
    std::size_t blocks_first_ = 0, blocks_page_ = 1;
    std::string blocks_followed_;
    ftxui::Box toc_box_{}, toc_up_box_{}, toc_down_box_{};
    std::size_t toc_first_ = 0, toc_page_ = 1;
    std::string toc_followed_;
    bool toc_focus_ = false, toc_follow_cursor_ = false, maximized_ = false;
    std::size_t toc_cursor_ = 0;
    bool fold_numbers_ = false;
    std::string notice_;
};

std::shared_ptr<TheoryScreen> make_theory_screen(UiContext& ui, std::shared_ptr<TheoryNavigator> nav, TheoryScreen::Mode mode);

}  // namespace signals::tui
