#include "tui/shell/subtabs.hpp"

#include <algorithm>

#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"

namespace signals::tui {
namespace {

class SubTabs final : public TabScreen {
public:
    SubTabs(std::string label, std::vector<std::pair<std::string, std::shared_ptr<TabScreen>>> children, std::size_t initial)
        : label_{std::move(label)}, children_{std::move(children)}, boxes_(children_.size()),
          active_{std::min(initial, children_.empty() ? std::size_t{0} : children_.size() - 1)} {}

    std::string tab_label() const override { return label_; }

    std::vector<Hint> hints() const override {
        auto out = child().hints();
        out.push_back({"[ ]", tr("tui.subtabs.hint")});
        return out;
    }

    std::string context() const override {
        std::string out;
        for (std::size_t i = 0; i < children_.size(); ++i) out += (i ? " " : "") + (i == active_ ? "[" + children_[i].first + "]" : children_[i].first);
        const auto inner = child().context();
        return inner.empty() ? out : out + " · " + inner;
    }

    ftxui::Element context_element(const UiContext& ui) override {
        using namespace ftxui;
        Elements parts;
        for (std::size_t i = 0; i < children_.size(); ++i) {
            if (i) parts.push_back(text(" "));
            auto label = text(i == active_ ? "[" + children_[i].first + "]" : children_[i].first);
            label = i == active_ ? label | bold | fg(ui.theme.accent, ui) : label | fg(ui.theme.fg, ui);
            parts.push_back(label | reflect(boxes_[i]));
        }
        if (const auto inner = child().context(); !inner.empty()) parts.push_back(text(" · " + inner) | fg(ui.theme.dim, ui));
        return hbox(std::move(parts));
    }

    void set_breakpoint(Breakpoint b) override {
        breakpoint_ = b;
        child().set_breakpoint(b);
    }
    bool capturing_text() const override { return child().capturing_text(); }
    std::vector<MenuItem> context_menu(int x, int y) override { return child().context_menu(x, y); }

    std::vector<JumpTarget> jump_targets() const override {
        auto out = child().jump_targets();
        for (std::size_t i = 0; i < children_.size(); ++i) out.push_back({boxes_[i], children_[i].first});
        return out;
    }

    void open_with(const NavArgs& args) override { child().open_with(args); }

    std::vector<Command> commands() override {
        auto out = child().commands();
        for (std::size_t i = 0; i < children_.size(); ++i)
            out.push_back({"subtab." + std::to_string(i), label_ + " ▸ " + children_[i].first, [this, i] { select_child(i); }});
        return out;
    }

    void select_child(std::size_t i) override {
        if (i >= children_.size()) return;
        active_ = i;
        child().set_breakpoint(breakpoint_);
    }

    ftxui::Element OnRender() override { return child().Render(); }

    bool OnEvent(ftxui::Event e) override {
        if (e.is_mouse()) {
            const auto& m = e.mouse();
            if (m.button == ftxui::Mouse::Left && m.motion == ftxui::Mouse::Pressed)
                for (std::size_t i = 0; i < boxes_.size(); ++i)
                    if (boxes_[i].Contain(m.x, m.y)) {
                        select_child(i);
                        return true;
                    }
            return child().OnEvent(e);
        }
        if (!child().capturing_text() && !children_.empty()) {
            if (e == ftxui::Event::Character(']')) {
                select_child((active_ + 1) % children_.size());
                return true;
            }
            if (e == ftxui::Event::Character('[')) {
                select_child((active_ + children_.size() - 1) % children_.size());
                return true;
            }
        }
        return child().OnEvent(e);
    }

private:
    TabScreen& child() const { return *children_[active_].second; }

    std::string label_;
    std::vector<std::pair<std::string, std::shared_ptr<TabScreen>>> children_;
    std::vector<ftxui::Box> boxes_;
    std::size_t active_ = 0;
    Breakpoint breakpoint_ = Breakpoint::primary;
};

}  // namespace

std::shared_ptr<TabScreen> make_subtabs(std::string label, std::vector<std::pair<std::string, std::shared_ptr<TabScreen>>> children,
                                        std::size_t initial) {
    return std::make_shared<SubTabs>(std::move(label), std::move(children), initial);
}

}  // namespace signals::tui
