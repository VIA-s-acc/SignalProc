#include "tui/widgets/text_util.hpp"
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
    SubTabs(std::string label, std::vector<std::pair<std::string, std::shared_ptr<TabScreen>>> children, std::size_t initial, SubTabKeys keys)
        : label_{std::move(label)}, children_{std::move(children)}, boxes_(children_.size()),
          active_{std::min(initial, children_.empty() ? std::size_t{0} : children_.size() - 1)}, keys_{keys} {}

    std::string tab_label() const override { return label_; }

    std::vector<Hint> hints() const override {
        auto out = child().hints();
        out.insert(out.begin(), {std::string{keys_.previous} + " " + keys_.next, tr("tui.subtabs.hint")});
        return out;
    }

    std::string context() const override {
        std::string out;
        for (std::size_t i = 0; i < children_.size(); ++i) out += (i ? " " : "") + (i == active_ ? "[" + children_[i].first + "]" : children_[i].first);
        const auto inner = child().context();
        return inner.empty() ? out : out + " · " + inner;
    }

    ftxui::Element context_element(const UiContext& ui, int width) override {
        using namespace ftxui;
        int names = 0;
        for (std::size_t i = 0; i < children_.size(); ++i) names += display_width(children_[i].first) + (i == active_ ? 2 : 0) + (i ? 1 : 0);
        const bool all = names <= width;
        Elements parts;
        int used = 0;
        for (auto& b : boxes_) b = ftxui::Box{1, 0, 1, 0};
        for (std::size_t i = 0; i < children_.size(); ++i) {
            if (!all && i != active_) continue;
            if (!parts.empty()) parts.push_back(text(" ")), ++used;
            const auto caption = i == active_ ? "[" + children_[i].first + "]" : children_[i].first;
            used += display_width(caption);
            auto label = text(caption);
            label = i == active_ ? label | bold | fg(ui.theme.accent, ui) : label | fg(ui.theme.fg, ui);
            parts.push_back(label | reflect(boxes_[i]));
        }
        const auto inner = child().context();
        const int room = width - used - 3;
        if (!inner.empty() && room >= 4) parts.push_back(text(" · " + ellipsize(inner, room, ui.caps.glyphs == GlyphMode::ascii)) | fg(ui.theme.dim, ui));
        return hbox(std::move(parts));
    }

    void set_breakpoint(Breakpoint b) override {
        breakpoint_ = b;
        child().set_breakpoint(b);
    }
    bool capturing_text() const override { return child().capturing_text(); }
    std::vector<MenuItem> context_menu(int x, int y) override { return child().context_menu(x, y); }
    std::optional<std::pair<int, int>> menu_anchor() const override { return child().menu_anchor(); }
    std::optional<TheoryLink> theory_link() const override { return child().theory_link(); }
    void publish(theory::LiveValues& v) const override { child().publish(v); }
    std::shared_ptr<TabScreen> active_child() const override { return children_[active_].second; }

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
            if (e == ftxui::Event::Character(keys_.next)) {
                select_child((active_ + 1) % children_.size());
                return true;
            }
            if (e == ftxui::Event::Character(keys_.previous)) {
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
    SubTabKeys keys_;
    Breakpoint breakpoint_ = Breakpoint::primary;
};

}  // namespace

std::shared_ptr<TabScreen> make_subtabs(std::string label, std::vector<std::pair<std::string, std::shared_ptr<TabScreen>>> children,
                                        std::size_t initial, SubTabKeys keys) {
    return std::make_shared<SubTabs>(std::move(label), std::move(children), initial, keys);
}

}  // namespace signals::tui
