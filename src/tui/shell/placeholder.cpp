#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/shell.hpp"

namespace signals::tui {
namespace {

class PlaceholderScreen final : public TabScreen {
public:
    PlaceholderScreen(std::string label, std::string title, std::string description, std::string stage, UiContext* ui)
        : label_{std::move(label)}, title_{std::move(title)}, description_{std::move(description)}, stage_{std::move(stage)}, ui_{ui} {}

    std::string tab_label() const override { return label_; }
    std::vector<Hint> hints() const override { return {}; }
    void set_breakpoint(Breakpoint) override {}

    ftxui::Element OnRender() override {
        static const UiContext fallback = [] {
            UiContext u;
            u.theme = builtin_theme("dark");
            return u;
        }();
        const UiContext& ui = ui_ ? *ui_ : fallback;
        auto body = ftxui::vbox({ftxui::text(description_), ftxui::text(""),
                                 ftxui::text(fmt::format(fmt::runtime(tr("tui.placeholder.stage")), fmt::arg("stage", stage_))) |
                                     fg(ui.theme.dim, ui)});
        return ftxui::vbox({ftxui::filler(), ftxui::hbox({ftxui::filler(), panel(title_, {}, body, false, ui), ftxui::filler()}),
                            ftxui::filler()});
    }

private:
    std::string label_, title_, description_, stage_;
    UiContext* ui_;
};

}  // namespace

std::shared_ptr<TabScreen> make_placeholder(std::string label, std::string title, std::string description, std::string stage,
                                            UiContext* ui) {
    return std::make_shared<PlaceholderScreen>(std::move(label), std::move(title), std::move(description), std::move(stage), ui);
}

std::vector<std::shared_ptr<TabScreen>> placeholder_screens(UiContext& ui) {
    return {
        make_placeholder(tr("tui.tab.signal"), tr("tui.placeholder.signal.title"), tr("tui.placeholder.signal.text"), "M3", &ui),
        make_placeholder(tr("tui.tab.spectrum"), tr("tui.placeholder.spectrum.title"), tr("tui.placeholder.spectrum.text"), "M4", &ui),
        make_placeholder(tr("tui.tab.filters"), tr("tui.placeholder.filters.title"), tr("tui.placeholder.filters.text"), "M3", &ui),
        make_placeholder(tr("tui.tab.bank"), tr("tui.placeholder.bank.title"), tr("tui.placeholder.bank.text"), "M4", &ui),
        make_placeholder(tr("tui.tab.decomposition"), tr("tui.placeholder.decomposition.title"), tr("tui.placeholder.decomposition.text"), "M4", &ui),
        make_placeholder(tr("tui.tab.matrix"), tr("tui.placeholder.matrix.title"), tr("tui.placeholder.matrix.text"), "M4", &ui),
        make_placeholder(tr("tui.tab.processing"), tr("tui.placeholder.processing.title"), tr("tui.placeholder.processing.text"), "M5", &ui),
        make_placeholder(tr("tui.tab.pipeline"), tr("tui.placeholder.pipeline.title"), tr("tui.placeholder.pipeline.text"), "M5", &ui),
        make_placeholder(tr("tui.tab.theory"), tr("tui.placeholder.theory.title"), tr("tui.placeholder.theory.text"), "M4", &ui),
    };
}

}  // namespace signals::tui
