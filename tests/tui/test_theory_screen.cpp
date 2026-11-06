#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include "theory/sections.hpp"
#include "tui/harness.hpp"
#include "tui/screens/theory_navigator.hpp"
#include "tui/screens/theory_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;

namespace {

struct TheoryFixture {
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<TheoryNavigator> nav = std::make_shared<TheoryNavigator>();
    std::shared_ptr<TheoryScreen> screen;
    Harness h;

    explicit TheoryFixture(TheoryScreen::Mode mode, int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)}, screen{std::make_shared<TheoryScreen>(*ui, nav, mode)}, h{screen, w, hgt - 3} {
        screen->set_breakpoint(classify(w, hgt));
        h.frame();
    }
    void type(std::string_view s) {
        for (char c : s) h.send(ftxui::Event::Character(c));
    }
};

}  // namespace

TEST_CASE("the tutorial opens a section and follows a link", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    f.nav->open({{TheoryLink::Kind::section, "dwt-mallat", ""}});
    auto frame = f.h.frame();
    CHECK(frame.find("11.1 DWT, пирамида Малла") != std::string::npos);
    CHECK(frame.find("{#dwt-mallat}") != std::string::npos);
    f.h.send(ftxui::Event::Tab);
    REQUIRE(f.screen->focused_link());
    f.h.send(ftxui::Event::Return);
    f.h.frame();
    CHECK(f.nav->current()->link.id != "dwt-mallat");
    f.h.send(ftxui::Event::Character('b'));
    f.h.frame();
    CHECK(f.nav->current()->link.id == "dwt-mallat");
    f.h.send(ftxui::Event::Character('f'));
    CHECK(f.nav->current()->link.id != "dwt-mallat");
}

TEST_CASE("search narrows the contents and enter opens the first hit", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    f.h.send(ftxui::Event::Character('/'));
    CHECK(f.screen->capturing_text());
    f.type("малла");
    const auto expected = signals::theory::search_sections("малла").size();
    auto frame = f.h.frame();
    CHECK(frame.find(std::to_string(expected) + " из 132") != std::string::npos);
    f.h.send(ftxui::Event::Return);
    CHECK_FALSE(f.screen->capturing_text());
    REQUIRE(f.nav->current());
    CHECK(f.nav->current()->link.id == "dwt-mallat");
}

TEST_CASE("search with no hits shows zero and keeps the selection valid", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    f.h.send(ftxui::Event::Character('/'));
    f.type("нетакогослова");
    f.h.send(ftxui::Event::ArrowDown);
    f.h.send(ftxui::Event::ArrowUp);
    CHECK(f.h.frame().find("0 из 132") != std::string::npos);
    f.h.send(ftxui::Event::Return);
    f.h.send(ftxui::Event::Escape);
    CHECK_FALSE(f.screen->capturing_text());
    f.h.frame();
}

TEST_CASE("the tutorial at 80x24 toggles the contents with t", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial, 80, 24};
    f.nav->open({{TheoryLink::Kind::section, "dwt-mallat", ""}});
    auto frame = f.h.frame();
    CHECK(frame.find("Малла") != std::string::npos);
    CHECK(frame.find("Соглашения") == std::string::npos);
    f.h.send(ftxui::Event::Character('t'));
    frame = f.h.frame();
    CHECK(frame.find("Соглашения") != std::string::npos);
}

TEST_CASE("clicking a contents entry opens it", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    const auto b = f.screen->area("toc:haar");
    REQUIRE(b.x_max >= b.x_min);
    f.h.click(b.x_min + 1, b.y_min);
    REQUIRE(f.nav->current());
    CHECK(f.nav->current()->link.id == "haar");
}

TEST_CASE("tutorial snapshots", "[tui][theory][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        TheoryFixture f{TheoryScreen::Mode::tutorial, w, h};
        f.nav->open({{TheoryLink::Kind::section, "dwt-mallat", ""}});
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("tutorial_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}
