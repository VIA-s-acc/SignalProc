#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>

#include "theory/blocks.hpp"
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

namespace {

std::shared_ptr<signals::theory::LiveValues> dwt_values() {
    auto v = std::make_shared<signals::theory::LiveValues>();
    v->source = "Разложение ▸ test.wav · db2 · J = 6";
    v->set("block.n", "450 686");
    v->set("block.wavelet", "db2");
    v->set("block.levels", "6");
    v->set("block.mode", "symmetric");
    v->set("block.filter_length", "4");
    v->set("block.max_level", "16");
    v->set("block.band_lengths", "a₆ 7 044 · d₆ 7 044 · d₅ 14 086 · d₄ 28 170 · d₃ 56 338 · d₂ 112 673 · d₁ 225 344");
    v->set("block.band_sum", "450 699 = N + 13");
    v->set("block.band_ranges", "a₆ 0–345 · d₆ 345–689 · d₅ 689–1378 Гц …");
    v->set("block.linear_phase", "нет");
    v->set("metric.multiplies", "3,6·10⁶");
    v->set_check({"check.round_trip", "", 1.6e-15, 1e-12});
    v->set_check({"check.orthogonality", "", 2.2e-16, 1e-12});
    v->set_check({"check.vanishing_moments", "", 4.4e-16, 1e-12});
    v->set_check({"check.parseval", "", 3.1e-15, 1e-12});
    return v;
}

}  // namespace

TEST_CASE("a card opened from the book shows dashes, not checks", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, std::make_shared<signals::theory::LiveValues>()});
    const auto frame = f.h.frame();
    CHECK(frame.find("11.1 DWT — пирамида Малла") != std::string::npos);
    CHECK(frame.find("── Формулы") != std::string::npos);
    CHECK(frame.find("открыто из: учебник") != std::string::npos);
    std::string card_text;
    for (int i = 0; i < 6; ++i) {
        const auto page = f.h.frame();
        card_text += page.substr(page.find('\n') + 1);
        f.h.send(ftxui::Event::PageDown);
    }
    CHECK(card_text.find("— нет данных") != std::string::npos);
    CHECK(card_text.find("✓") == std::string::npos);
    CHECK(card_text.find("F1 на экране") != std::string::npos);
}

TEST_CASE("a card with a publisher follows its source", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    std::string wavelet = "db2";
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, std::make_shared<signals::theory::LiveValues>(), [&](signals::theory::LiveValues& v) {
                     v.source = "Разложение ▸ test.wav";
                     v.set("block.wavelet", wavelet);
                 }});
    auto frame = f.h.frame();
    CHECK(frame.find("фильтр db2") != std::string::npos);
    CHECK(frame.find("открыто из: Разложение ▸ test.wav") != std::string::npos);
    wavelet = "sym4";
    frame = f.h.frame();
    CHECK(frame.find("фильтр sym4") != std::string::npos);
    CHECK(frame.find("F1 на экране") == std::string::npos);
}

TEST_CASE("view buttons navigate with the bank", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    std::string target;
    NavArgs args;
    f.ui->navigate = [&](std::string_view t, const NavArgs& a) {
        target = std::string{t};
        args = a;
    };
    auto values = std::make_shared<signals::theory::LiveValues>();
    values->set("block.bank", "db4");
    values->set("block.wavelet", "db4");
    values->set("block.n", "64");
    f.nav->open({{TheoryLink::Kind::card, "bank_matrix", ""}, values});
    f.h.frame();
    for (int i = 0; i < 40 && f.screen->focused_target() != "view:matrix"; ++i) {
        f.h.send(ftxui::Event::Tab);
        f.h.frame();
    }
    REQUIRE(f.screen->focused_target() == "view:matrix");
    const auto b = f.screen->area("view:matrix");
    REQUIRE(b.y_max >= b.y_min);
    CHECK(f.h.click(b.x_min, b.y_min));
    CHECK(target == "matrix");
    CHECK(args.wavelet == "db4");
    REQUIRE(args.n);
    CHECK(*args.n == 64);
    CHECK(f.h.frame().find("Блок в графе") != std::string::npos);
}

TEST_CASE("o opens the source and esc returns to the origin", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    std::filesystem::path opened;
    std::string target;
    f.ui->repo_root = SIGNALS_SOURCE_DIR;
    f.ui->open_in_editor = [&](std::filesystem::path p) { opened = std::move(p); };
    f.ui->navigate = [&](std::string_view t, const NavArgs&) { target = std::string{t}; };
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, dwt_values(), {}, "decomposition"});
    f.h.frame();
    f.h.send(ftxui::Event::End);
    auto frame = f.h.frame();
    CHECK(frame.find("src/core/dwt.hpp · dwt_level()") != std::string::npos);
    f.h.send(ftxui::Event::Character('o'));
    CHECK(opened == std::filesystem::path{SIGNALS_SOURCE_DIR} / "src/core/dwt.hpp");
    f.h.send(ftxui::Event::Escape);
    CHECK(target == "decomposition");
    f.ui->repo_root = "/nonexistent/signals";
    f.h.send(ftxui::Event::Character('o'));
    CHECK(f.h.frame().find("исходники не найдены") != std::string::npos);
}

TEST_CASE("copy LaTeX from the formula menu", "[tui][theory][menu]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    std::string copied;
    f.ui->copy_text = [&](std::string s) { copied = std::move(s); };
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, dwt_values()});
    f.h.frame();
    const auto b = f.screen->area("formula:0");
    REQUIRE(b.x_max >= b.x_min);
    auto items = f.screen->context_menu(b.x_min + 2, b.y_min);
    REQUIRE(items.size() == 1);
    CHECK(items[0].label == "Копировать LaTeX");
    items[0].action();
    CHECK(copied.starts_with("a_j[k]"));
}

TEST_CASE("g switches formulas to ascii", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, dwt_values()});
    CHECK(f.h.frame().find("∑") != std::string::npos);
    f.h.send(ftxui::Event::Character('g'));
    const auto frame = f.h.frame();
    CHECK(frame.find("∑") == std::string::npos);
    CHECK(frame.find("sum") != std::string::npos);
    CHECK(frame.find("[ASCII]") != std::string::npos);
}

TEST_CASE("n folds the live numbers section", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, dwt_values()});
    CHECK(f.h.frame().find("Длины полос") != std::string::npos);
    f.h.send(ftxui::Event::Character('n'));
    const auto frame = f.h.frame();
    CHECK(frame.find("Длины полос") == std::string::npos);
    CHECK(frame.find("── С числами этого блока") != std::string::npos);
}

TEST_CASE("under the hood snapshots", "[tui][theory][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        TheoryFixture f{TheoryScreen::Mode::under_the_hood, w, h};
        f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, dwt_values()});
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("under_the_hood_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("the references link opens the bibliography", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    const auto b = f.screen->area("references");
    REQUIRE(b.x_max >= b.x_min);
    f.h.click(b.x_min, b.y_min);
    REQUIRE(f.nav->current());
    CHECK(f.nav->current()->link.id == "references");
    CHECK(f.h.frame().find("Литература") != std::string::npos);
}

TEST_CASE("under the hood marks sections with cards and opens them from the contents", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    std::string target;
    f.ui->navigate = [&](std::string_view t, const NavArgs&) { target = std::string{t}; };
    f.nav->open({{TheoryLink::Kind::card, "threshold", ""}, std::make_shared<signals::theory::LiveValues>()});
    auto frame = f.h.frame();
    CHECK(frame.find("◈") != std::string::npos);
    f.nav->open({{TheoryLink::Kind::card, "idwt", ""}, std::make_shared<signals::theory::LiveValues>()});
    f.h.frame();
    const auto b = f.screen->area("toc:dwt-mallat");
    REQUIRE(b.x_max >= b.x_min);
    f.h.click(b.x_min + 1, b.y_min);
    REQUIRE(f.nav->last(TheoryLink::Kind::card));
    CHECK(f.nav->last(TheoryLink::Kind::card)->link.id == "dwt");
    const auto t = f.screen->area("toc:transforms");
    REQUIRE(t.x_max >= t.x_min);
    f.h.click(t.x_min + 1, t.y_min);
    CHECK(target == "tutorial");
    CHECK(f.nav->current()->link.id == "transforms");
}

TEST_CASE("scrolling stops at the end of the document", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, dwt_values()});
    f.h.frame();
    for (int i = 0; i < 40; ++i) f.h.send(ftxui::Event::PageDown);
    const int end = f.screen->scroll();
    auto frame = f.h.frame();
    CHECK(frame.find("o — открыть") != std::string::npos);
    CHECK(frame.find("── Реализация") != std::string::npos);
    f.h.send(ftxui::Event::PageDown);
    f.h.send(ftxui::Event::ArrowDown);
    CHECK(f.screen->scroll() == end);
    f.h.send(ftxui::Event::PageUp);
    CHECK(f.screen->scroll() < end);
}

TEST_CASE("the dwt card sends its bank but not the signal length to the matrix", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    std::string target;
    NavArgs args;
    f.ui->navigate = [&](std::string_view t, const NavArgs& a) {
        target = std::string{t};
        args = a;
    };
    f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, dwt_values()});
    f.h.frame();
    for (int i = 0; i < 40 && f.screen->focused_target() != "view:matrix"; ++i) {
        f.h.send(ftxui::Event::Tab);
        f.h.frame();
    }
    REQUIRE(f.screen->focused_target() == "view:matrix");
    f.h.send(ftxui::Event::Return);
    CHECK(target == "matrix");
    CHECK(args.wavelet == "db2");
    CHECK_FALSE(args.n);
}

TEST_CASE("under the hood hints name n, o and Esc", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    std::string keys;
    for (const auto& h : f.screen->hints()) keys += h.key + " ";
    CHECK(keys.find("n ") != std::string::npos);
    CHECK(keys.find("o ") != std::string::npos);
    CHECK(keys.find("Esc") != std::string::npos);
}

TEST_CASE("the block list shows its range and scrolls to every block", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood};
    f.nav->open({{TheoryLink::Kind::card, "gen", ""}, std::make_shared<signals::theory::LiveValues>()});
    auto frame = f.h.frame();
    const auto total = signals::theory::block_catalog().size();
    CHECK(frame.find("из " + std::to_string(total)) != std::string::npos);
    CHECK(frame.find("ещё") != std::string::npos);
    const std::string last{signals::theory::block_catalog().back().id};
    const auto absent = [&](const std::string& id) { const auto b = f.screen->area(id); return b.x_max < b.x_min; };
    CHECK(absent("block:" + last));

    const auto list = f.screen->area("blocks");
    REQUIRE(list.x_max >= list.x_min);
    for (int i = 0; i < 40; ++i) f.h.wheel(list.x_min + 1, list.y_min + 1, false);
    frame = f.h.frame();
    const auto b = f.screen->area("block:" + last);
    REQUIRE(b.x_max >= b.x_min);
    f.h.click(b.x_min + 1, b.y_min);
    REQUIRE(f.nav->last(TheoryLink::Kind::card));
    CHECK(f.nav->last(TheoryLink::Kind::card)->link.id == last);

    f.h.frame();
    const auto moved = f.screen->area("blocks");
    for (int i = 0; i < 40; ++i) f.h.wheel(moved.x_min + 1, moved.y_min + 1, true);
    f.h.frame();
    const std::string head{signals::theory::block_catalog().front().id};
    const auto first = f.screen->area("block:" + head);
    CHECK(first.x_max >= first.x_min);
    const auto down = f.screen->area("blocks:down");
    REQUIRE(down.x_max >= down.x_min);
    f.h.click(down.x_min + 1, down.y_min);
    f.h.frame();
    CHECK(absent("block:" + head));
}

TEST_CASE("the tutorial opens the block card of a section under the hood", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    std::string target;
    f.ui->navigate = [&](std::string_view t, const NavArgs&) { target = std::string{t}; };
    const auto absent = [&](const std::string& id) { const auto b = f.screen->area(id); return b.x_max < b.x_min; };

    const signals::theory::BlockSpec* expected = nullptr;
    std::size_t cards = 0;
    for (const auto& b : signals::theory::block_catalog())
        if (b.section == "conv-signals") {
            if (!expected) expected = &b;
            ++cards;
        }
    REQUIRE(expected);
    REQUIRE(cards > 1);
    f.nav->open({{TheoryLink::Kind::section, "conv-signals"}});
    auto frame = f.h.frame();
    CHECK(frame.find("◈") != std::string::npos);
    CHECK(frame.find(std::string{signals::theory::find_card(expected->id)->title}) != std::string::npos);
    CHECK(frame.find("ещё " + std::to_string(cards - 1)) != std::string::npos);
    CHECK(f.h.send(ftxui::Event::Character('i')));
    CHECK(target == "under_the_hood");
    REQUIRE(f.nav->last(TheoryLink::Kind::card));
    CHECK(f.nav->last(TheoryLink::Kind::card)->link.id == expected->id);
    CHECK(f.nav->last(TheoryLink::Kind::card)->origin == "tutorial");

    target.clear();
    f.nav->open({{TheoryLink::Kind::section, "dwt-mallat"}});
    f.h.frame();
    const auto link = f.screen->area("card_link");
    REQUIRE(link.x_max >= link.x_min);
    f.h.click(link.x_min + 1, link.y_min);
    CHECK(target == "under_the_hood");
    CHECK(f.nav->last(TheoryLink::Kind::card)->link.id == "dwt");

    target.clear();
    f.nav->open({{TheoryLink::Kind::section, "transforms"}});
    f.h.frame();
    CHECK(absent("card_link"));
    CHECK_FALSE(f.h.send(ftxui::Event::Character('i')));
    CHECK(target.empty());
}

TEST_CASE("a contents list that does not fit scrolls with the wheel", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial, 80, 24};
    f.h.send(ftxui::Event::Character('/'));
    f.h.send(ftxui::Event::Character("и"));
    auto frame = f.h.frame();
    const auto absent = [&](const std::string& id) { const auto b = f.screen->area(id); return b.x_max < b.x_min; };
    CHECK(frame.find("ещё") != std::string::npos);
    CHECK(absent("toc:up"));
    REQUIRE_FALSE(absent("toc:down"));
    const auto list = f.screen->area("toc");
    for (int i = 0; i < 5; ++i) f.h.wheel(list.x_min + 1, list.y_min + 1, false);
    f.h.frame();
    CHECK_FALSE(absent("toc:up"));
    for (int i = 0; i < 400; ++i) f.h.wheel(list.x_min + 1, list.y_min + 1, false);
    f.h.frame();
    CHECK(absent("toc:down"));
    for (int i = 0; i < 400; ++i) f.h.wheel(list.x_min + 1, list.y_min + 1, true);
    f.h.frame();
    CHECK(absent("toc:up"));
    const auto down = f.screen->area("toc:down");
    f.h.click(down.x_min + 1, down.y_min);
    f.h.frame();
    CHECK_FALSE(absent("toc:up"));
}

TEST_CASE("under the hood in a small terminal shows the contents until a card is open", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood, 117, 40};
    REQUIRE(f.screen->hints().front().key == "↑↓");
    REQUIRE(f.screen->hints()[1].key == "Enter");
    auto frame = f.h.frame();
    CHECK(frame.find("Блоки") != std::string::npos);
    CHECK(frame.find("слева") == std::string::npos);
    const auto b = f.screen->area("block:dwt");
    REQUIRE(b.x_max >= b.x_min);
    f.h.click(b.x_min + 1, b.y_min);
    frame = f.h.frame();
    CHECK(frame.find("Блоки") == std::string::npos);
    CHECK(f.nav->last(TheoryLink::Kind::card)->link.id == "dwt");
}

TEST_CASE("a chapter is marked when one of its subsections has a card", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    f.nav->open({{TheoryLink::Kind::section, "conv-signals"}});
    const auto frame = f.h.frame();
    const auto contents_line = [&](std::string_view needle) {
        const auto at = frame.find(needle);
        REQUIRE(at != std::string::npos);
        return frame.substr(at, frame.find("│", at) - at);
    };
    CHECK(contents_line("Варианты преобраз").find("◈") != std::string::npos);
    CHECK(contents_line("Сводный план").find("◈") == std::string::npos);
}

TEST_CASE("the contents can be walked with the keyboard", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    f.nav->open({{TheoryLink::Kind::section, "conv-signals"}});
    f.h.frame();
    const auto& all = signals::theory::sections();
    const auto here = signals::theory::index_of(*signals::theory::find_section("conv-signals"));
    REQUIRE(all[here].parent);
    const auto siblings = signals::theory::children_of(*all[here].parent);
    REQUIRE(siblings.size() > 1);
    REQUIRE(siblings[0] == here);
    CHECK(f.h.send(ftxui::Event::Character('t')));
    f.h.frame();
    f.h.send(ftxui::Event::ArrowDown);
    f.h.frame();
    f.h.send(ftxui::Event::Return);
    CHECK(f.nav->current()->link.id == all[siblings[1]].id);

    f.h.send(ftxui::Event::Character('t'));
    f.h.send(ftxui::Event::End);
    f.h.send(ftxui::Event::Escape);
    f.h.send(ftxui::Event::Return);
    CHECK(f.nav->current()->link.id == all[siblings[1]].id);
}

TEST_CASE("search results can be paged with the keyboard", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::tutorial};
    f.h.send(ftxui::Event::Character('/'));
    f.h.send(ftxui::Event::Character("и"));
    f.h.frame();
    f.h.send(ftxui::Event::End);
    f.h.frame();
    f.h.send(ftxui::Event::Return);
    const auto hits = signals::theory::search_sections("и");
    REQUIRE(hits.size() > 20);
    CHECK(f.nav->current()->link.id == signals::theory::sections()[hits.back()].id);
}

TEST_CASE("under the hood blocks open from the keyboard in a small terminal", "[tui][theory]") {
    TheoryFixture f{TheoryScreen::Mode::under_the_hood, 80, 24};
    const auto absent = [&](const std::string& id) { const auto b = f.screen->area(id); return b.x_max < b.x_min; };
    f.h.frame();
    f.h.send(ftxui::Event::End);
    f.h.frame();
    const std::string last{signals::theory::block_catalog().back().id};
    CHECK_FALSE(absent("block:" + last));
    f.h.send(ftxui::Event::Return);
    REQUIRE(f.nav->last(TheoryLink::Kind::card));
    CHECK(f.nav->last(TheoryLink::Kind::card)->link.id == last);
    CHECK(f.h.frame().find("Блоки") == std::string::npos);
    f.h.send(ftxui::Event::Character('t'));
    CHECK(f.h.frame().find("Блоки") != std::string::npos);
    f.h.send(ftxui::Event::Escape);
    CHECK(f.h.frame().find("Блоки") == std::string::npos);
}

TEST_CASE("z maximizes the focused theory panel and esc restores it", "[tui][theory]") {
    for (auto mode : {TheoryScreen::Mode::tutorial, TheoryScreen::Mode::under_the_hood}) {
        TheoryFixture f{mode};
        if (mode == TheoryScreen::Mode::under_the_hood) f.nav->open({{TheoryLink::Kind::card, "dwt", ""}, std::make_shared<signals::theory::LiveValues>()});
        else f.nav->open({{TheoryLink::Kind::section, "dwt-mallat"}});
        const auto absent = [&](const std::string& id) { const auto b = f.screen->area(id); return b.x_max < b.x_min; };
        f.h.frame();
        REQUIRE_FALSE(absent("toc"));
        const auto narrow = f.screen->area("doc");
        CHECK(f.h.send(ftxui::Event::Character('z')));
        f.h.frame();
        CHECK(absent("toc"));
        CHECK(f.screen->area("doc").x_max - f.screen->area("doc").x_min > narrow.x_max - narrow.x_min);
        f.h.send(ftxui::Event::Character('t'));
        f.h.frame();
        CHECK_FALSE(absent("toc"));
        CHECK(absent("doc"));
        CHECK(f.h.send(ftxui::Event::Escape));
        f.h.frame();
        CHECK_FALSE(absent("toc"));
        CHECK_FALSE(absent("doc"));
    }
}
