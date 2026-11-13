#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>

#include <ftxui/component/event.hpp>

#include "support/paths.hpp"
#include "theory/blocks.hpp"
#include "theory/cards.hpp"
#include "theory/live.hpp"
#include "tui/app_screens.hpp"
#include "tui/harness.hpp"
#include "tui/screens/theory_navigator.hpp"
#include "tui/screens/theory_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

struct AppFixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui = signals::test::make_ui(150, 44);
    AppModels models = make_app_models();
    std::vector<std::shared_ptr<TabScreen>> screens;
    Harness h;

    AppFixture() : h{(ui->jobs = &jobs, models.signal->open(signals::test::sample("test.wav"), jobs), screens = make_app_screens(*ui, models),
                      make_shell(*ui, screens, [] {})),
                     150, 44} {
        h.frame();
    }
    std::string page_text() {
        std::string out;
        for (int i = 0; i < 8; ++i) {
            out += h.frame();
            h.send(ftxui::Event::PageDown);
        }
        return out;
    }
};

}  // namespace

TEST_CASE("F1 on the decomposition opens the dwt card with test.wav numbers", "[tui][f1]") {
    AppFixture app;
    app.h.send(ftxui::Event::Character('5'));
    app.h.frame();
    app.h.send(ftxui::Event::F1);
    const auto first = app.h.frame();
    CHECK(first.find("11.1 DWT — пирамида Малла") != std::string::npos);
    CHECK(first.find("открыто из: Разлож.") != std::string::npos);
    const auto text = app.page_text();
    CHECK(text.find("450 686") != std::string::npos);
    CHECK(text.find("225 344") != std::string::npos);
    CHECK(text.find("✓") != std::string::npos);
}

TEST_CASE("every publisher fills every value its card uses", "[tui][f1]") {
    AppFixture app;
    for (char tab : {'1', '3', '4', '5', '6'}) {
        app.h.send(ftxui::Event::Character(tab));
        app.h.frame();
        std::shared_ptr<TabScreen> target = app.screens[static_cast<std::size_t>(tab - '1')];
        while (auto c = target->active_child()) target = c;
        const auto link = target->theory_link();
        INFO("tab " << tab);
        REQUIRE(link);
        REQUIRE(link->kind == TheoryLink::Kind::card);
        signals::theory::LiveValues v;
        target->publish(v);
        CHECK_FALSE(v.source.empty());
        const auto* block = signals::theory::find_block(link->id);
        REQUIRE(block);
        for (auto name : block->values) {
            INFO(link->id << ": " << name);
            CHECK(v.find(name) != nullptr);
        }
        if (const auto* card = signals::theory::find_card(link->id))
            for (const auto& s : card->sections)
                for (const auto& p : signals::theory::placeholders(s.body)) {
                    INFO(link->id << ": " << p);
                    CHECK(v.find(p) != nullptr);
                }
    }
}

TEST_CASE("F1 on the explanation opens the Haar chapter", "[tui][f1]") {
    AppFixture app;
    app.h.send(ftxui::Event::Character('9'));
    app.h.frame();
    app.h.send(ftxui::Event::F1);
    const auto frame = app.h.frame();
    CHECK(frame.find("5 Учебный пример: банк Хаара") != std::string::npos);
    CHECK(app.models.theory->current()->link.id == "haar");
}

TEST_CASE("changing the wavelet updates an open card", "[tui][f1]") {
    AppFixture app;
    app.h.send(ftxui::Event::Character('5'));
    app.h.frame();
    app.h.send(ftxui::Event::F1);
    CHECK(app.page_text().find("фильтр db2") != std::string::npos);
    auto o = app.models.decomposition->options();
    o.wavelet = "db4";
    app.models.decomposition->set_options(o, app.jobs);
    for (int i = 0; i < 8; ++i) app.h.send(ftxui::Event::PageUp);
    CHECK(app.page_text().find("фильтр db4") != std::string::npos);
}

TEST_CASE("a closed source leaves a snapshot", "[tui][f1]") {
    auto ui = signals::test::make_ui(150, 44);
    auto nav = std::make_shared<TheoryNavigator>();
    struct Source final : TabScreen {
        std::string tab_label() const override { return "src"; }
        std::vector<Hint> hints() const override { return {}; }
        void set_breakpoint(Breakpoint) override {}
        std::optional<TheoryLink> theory_link() const override { return TheoryLink{TheoryLink::Kind::card, "dwt"}; }
        void publish(signals::theory::LiveValues& v) const override {
            v.source = "Источник";
            v.set("block.wavelet", "db2");
        }
    };
    auto source = std::make_shared<Source>();
    TheoryPage page = make_theory_page(source, "tab:5");
    auto screen = make_theory_screen(*ui, nav, TheoryScreen::Mode::under_the_hood);
    Harness h{screen, 150, 41};
    nav->open(std::move(page));
    CHECK(h.frame().find("открыто из: Источник") != std::string::npos);
    source.reset();
    const auto frame = h.frame();
    CHECK(frame.find("снимок") != std::string::npos);
    CHECK(frame.find("Источник") != std::string::npos);
}

TEST_CASE("every tab answers F1 without a crash", "[tui][f1]") {
    for (auto [w, hgt] : {std::pair{150, 44}, std::pair{80, 24}}) {
        JobRunner jobs{[] {}, 0ms, true};
        auto ui = signals::test::make_ui(w, hgt);
        ui->jobs = &jobs;
        auto models = make_app_models();
        models.signal->open(signals::test::sample("test.wav"), jobs);
        auto screens = make_app_screens(*ui, models);
        Harness shell{make_shell(*ui, screens, [] {}), w, hgt};
        const std::pair<char, int> tabs[]{{'1', 1}, {'2', 1}, {'3', 1}, {'4', 2}, {'5', 1}, {'6', 1}, {'7', 1}, {'8', 1}, {'9', 4}};
        for (const auto& [key, subtabs] : tabs)
            for (int s = 0; s < subtabs; ++s) {
                shell.send(ftxui::Event::Character(key));
                for (int k = 0; k < s; ++k) shell.send(ftxui::Event::Character(']'));
                shell.frame();
                shell.send(ftxui::Event::F1);
                const auto frame = shell.frame();
                INFO("size " << w << "x" << hgt << " tab " << key << " sub " << s);
                CHECK(frame.find("nan") == std::string::npos);
                CHECK(frame.find("inf") == std::string::npos);
                CHECK(frame.find("формула не разобрана") == std::string::npos);
                shell.send(ftxui::Event::Escape);
                shell.frame();
            }
    }
}

TEST_CASE("parseval and orthogonality are checks only where they must hold", "[tui][f1]") {
    AppFixture app;
    app.h.send(ftxui::Event::Character('5'));
    app.h.frame();
    auto publish = [&] {
        signals::theory::LiveValues v;
        app.screens[4]->publish(v);
        return v;
    };
    auto v = publish();
    REQUIRE(v.find("check.parseval"));
    CHECK_FALSE(v.find("check.parseval")->check);
    CHECK(v.find("check.parseval")->text.find("≈") != std::string::npos);
    REQUIRE(v.find("check.orthogonality"));
    CHECK(v.find("check.orthogonality")->check);
    auto o = app.models.decomposition->options();
    o.mode = signals::core::Extension::periodization;
    o.levels = 1;
    app.models.decomposition->set_options(o, app.jobs);
    app.h.frame();
    v = publish();
    REQUIRE(v.find("check.parseval")->check);
    CHECK(v.find("check.parseval")->check->passed());
    o.wavelet = "bior2.2";
    app.models.decomposition->set_options(o, app.jobs);
    app.h.frame();
    v = publish();
    CHECK_FALSE(v.find("check.orthogonality")->check);
    CHECK_FALSE(v.find("check.parseval")->check);
}
