#include "tui/app_screens.hpp"

#include "tui/screens/pipeline_screen.hpp"

#include <optional>
#include <utility>

#include "core/bank.hpp"
#include "i18n/i18n.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/screens/decomposition_screen.hpp"
#include "tui/screens/designer_screen.hpp"
#include "tui/screens/explain_screen.hpp"
#include "tui/screens/filters_screen.hpp"
#include "tui/screens/matrix_screen.hpp"
#include "tui/screens/signal_screen.hpp"
#include "tui/screens/theory_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/shell/subtabs.hpp"

namespace signals::tui {

AppModels make_app_models() {
    AppModels m;
    m.signal = std::make_shared<SignalModel>();
    m.comparison = std::make_shared<ComparisonSet>();
    for (auto role : {FilterRole::h0, FilterRole::h1}) (void)m.comparison->add({FilterRef::Kind::bank, "db2", role});
    m.designer = std::make_shared<DesignerModel>();
    m.decomposition = std::make_shared<DecompositionModel>(m.signal);
    m.matrix = std::make_shared<MatrixModel>();
    m.theory = std::make_shared<TheoryNavigator>();
    m.pipeline = std::make_shared<PipelineModel>();
    return m;
}

std::vector<std::shared_ptr<TabScreen>> make_app_screens(UiContext& ui, AppModels& models) {
    auto screens = placeholder_screens(ui);
    screens[0] = make_signal_screen(ui, models.signal);
    screens[2] = make_filters_screen(ui, models.comparison);
    if (ui.jobs) models.designer->set_p(3, *ui.jobs);
    screens[3] = make_subtabs(tr("tui.tab.bank"), {{tr("tui.sub.designer"), make_designer_screen(ui, models.designer)},
                                                  {tr("tui.sub.diagram"), make_placeholder(tr("tui.sub.diagram"), tr("tui.placeholder.diagram.title"),
                                                                                           tr("tui.placeholder.diagram.text"), "M6", &ui)}});
    screens[4] = make_decomposition_screen(ui, models.decomposition);
    screens[5] = make_matrix_screen(ui, models.matrix);
    screens[7] = make_pipeline_screen(ui, models.pipeline, [signal = models.signal]() -> std::optional<std::filesystem::path> {
        const auto d = signal->data();
        return d ? std::optional{d->path} : std::nullopt;
    });
    ui.open_theory = [nav = models.theory, &ui](TheoryPage page) {
        const bool card = page.link.kind == TheoryLink::Kind::card;
        nav->open(std::move(page));
        if (ui.navigate) ui.navigate(card ? "under_the_hood" : "tutorial", {});
    };
    auto user_bank = [] {
        const auto banks = user_banks();
        if (banks.empty()) return std::optional<core::FilterBank>{};
        auto bank = core::make_bank<double>(banks.back().name, banks.back().h0, banks.back().f0, 1e-9);
        return bank ? std::optional<core::FilterBank>{std::move(*bank)} : std::nullopt;
    };
    screens[8] = make_subtabs(tr("tui.tab.theory"),
                              {{tr("tui.sub.explain"), make_explain_screen(ui, user_bank)},
                               {tr("tui.sub.under_the_hood"), make_theory_screen(ui, models.theory, TheoryScreen::Mode::under_the_hood)},
                               {tr("tui.sub.tutorial"), make_theory_screen(ui, models.theory, TheoryScreen::Mode::tutorial)}});
    return screens;
}

}  // namespace signals::tui
