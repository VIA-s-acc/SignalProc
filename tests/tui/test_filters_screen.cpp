#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>

#include <chrono>
#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>

#include "tui/harness.hpp"
#include "tui/screens/filters_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using Kind = FilterRef::Kind;

namespace {

struct Fixture {
    std::shared_ptr<UiContext> ui;
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    std::shared_ptr<ComparisonSet> set = std::make_shared<ComparisonSet>();
    std::shared_ptr<FiltersScreen> screen;

    explicit Fixture(int w = 150, int h = 44, GlyphMode glyphs = GlyphMode::unicode, ColorDepth depth = ColorDepth::truecolor)
        : ui{signals::test::make_ui(w, h, glyphs, depth)} {
        ui->jobs = &jobs;
        screen = std::make_shared<FiltersScreen>(*ui, set);
        screen->set_breakpoint(classify(w, h));
    }
};

std::string line_with(const std::string& frame, const std::string& needle) {
    std::istringstream in{frame};
    for (std::string line; std::getline(in, line);)
        if (line.find(needle) != std::string::npos) return line;
    return {};
}

int column_of(const FiltersScreen& s, double w) {
    const auto a = s.area("magnitude_plot");
    const int columns = a.x_max - a.x_min + 1;
    const auto [lo, hi] = s.view();
    const Axis axis = hi - lo >= 0.5 ? Axis{lo * std::numbers::pi, hi * std::numbers::pi, AxisScale::pi} : Axis{lo, hi};
    return a.x_min + column_of_value(axis, hi - lo >= 0.5 ? w * std::numbers::pi : w, columns);
}

}  // namespace

TEST_CASE("choosing a wavelet from the library adds and analyses it", "[tui][filters]") {
    Fixture f;
    Harness h{f.screen, 150, 41};
    h.frame();
    h.send(ftxui::Event::Character('/'));
    CHECK(f.screen->capturing_text());
    for (char c : std::string{"db4"}) h.send(ftxui::Event::Character(c));
    CHECK(h.frame().find("1 из") != std::string::npos);
    h.send(ftxui::Event::Return);
    CHECK_FALSE(f.screen->capturing_text());
    REQUIRE(f.set->entries().size() == 1);
    CHECK(f.set->entries()[0].ref.label() == "h₀ db4");
    const auto frame = h.frame();
    REQUIRE(f.set->entries()[0].analysis);
    CHECK(line_with(frame, "Сравнение · 1 из 5").size() > 0);
    CHECK(frame.find("h₀ db4 (анализ НЧ)") != std::string::npos);
    CHECK(frame.find("Свойства · h₀ db4") != std::string::npos);

    h.send(ftxui::Event::Character('t'));
    h.send(ftxui::Event::Character('/'));
    for (char c : std::string{"db4"}) h.send(ftxui::Event::Character(c));
    h.send(ftxui::Event::Return);
    REQUIRE(f.set->entries().size() == 2);
    CHECK(f.set->entries()[1].ref.label() == "h₁ db4");
    CHECK(f.set->focus() == 1u);
}

TEST_CASE("frequency cursors read every visible trace", "[tui][filters]") {
    Fixture f;
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h1}));
    Harness h{f.screen, 150, 41};
    h.frame();
    const auto a = f.screen->area("magnitude_plot");
    REQUIRE(a.x_max > a.x_min);
    const int columns = a.x_max - a.x_min + 1;
    h.click(column_of(*f.screen, 0.5), (a.y_min + a.y_max) / 2);
    REQUIRE(f.screen->cursor_a());
    CHECK(std::abs(*f.screen->cursor_a() - 0.5) <= 1.0 / columns);
    auto readout = line_with(h.frame(), "A ω =");
    CHECK(readout.find("h₀ db2") != std::string::npos);
    CHECK(readout.find("h₁ db2") != std::string::npos);
    f.screen->set_cursor_a(0.5);
    readout = line_with(h.frame(), "A ω =");
    CHECK(readout.find("h₀ db2 −3,01") != std::string::npos);
    CHECK(readout.find("h₁ db2 −3,01") != std::string::npos);
    h.click(column_of(*f.screen, 0.25), (a.y_min + a.y_max) / 2, true);
    const auto b = line_with(h.frame(), "B ω =");
    CHECK(b.find("Δ") != std::string::npos);
    h.send(ftxui::Event::Character('n'));
    CHECK(line_with(h.frame(), "A ω =").find("h₀ db2 0,00") != std::string::npos);
}

TEST_CASE("wheel zoom keeps the frequency under the mouse", "[tui][filters]") {
    Fixture f;
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    Harness h{f.screen, 150, 41};
    h.frame();
    const auto a = f.screen->area("magnitude_plot");
    const int x = column_of(*f.screen, 0.25), y = (a.y_min + a.y_max) / 2;
    const double before = f.screen->omega_at("magnitude", x);
    h.wheel(x, y, true);
    h.frame();
    auto [lo, hi] = f.screen->view();
    CHECK(hi - lo == Catch::Approx(1 / 1.25));
    const int columns = a.x_max - a.x_min + 1;
    CHECK(std::abs(f.screen->omega_at("magnitude", x) - before) <= (hi - lo) / columns);
    for (int k = 0; k < 60; ++k) h.wheel(x, y, true);
    h.frame();
    std::tie(lo, hi) = f.screen->view();
    CHECK(hi - lo == Catch::Approx(16.0 / FiltersScreen::kPoints));
    CHECK(f.set->entries()[0].view);
    CHECK(h.frame().find("nan") == std::string::npos);
    const double edge = f.screen->omega_at("magnitude", x);
    h.wheel(x, y, true, true);
    std::tie(lo, hi) = f.screen->view();
    CHECK(hi - lo == Catch::Approx(16.0 / FiltersScreen::kPoints));
    CHECK(f.screen->omega_at("magnitude", x) == Catch::Approx(edge));
    for (int k = 0; k < 60; ++k) h.wheel(x, y, false);
    std::tie(lo, hi) = f.screen->view();
    CHECK(lo == 0.0);
    CHECK(hi == Catch::Approx(1.0));
}

TEST_CASE("group delay masks points near zeros", "[tui][filters]") {
    Fixture f;
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h1}));
    Harness h{f.screen, 150, 41};
    const auto frame = h.frame();
    CHECK(frame.find("ГВЗ τ(ω), отсчёты") != std::string::npos);
    CHECK(frame.find("nan") == std::string::npos);
    const auto& r = f.set->entries()[0].analysis->response;
    const auto gd = masked_group_delay(r, 1e-3);
    CHECK(std::isnan(gd[1]));
    CHECK(std::isfinite(r.group_delay[1]));
}

TEST_CASE("comparison keys toggle, mark the reference and remove", "[tui][filters]") {
    Fixture f;
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h1}));
    Harness h{f.screen, 150, 41};
    h.frame();
    h.send(ftxui::Event::Character(' '));
    CHECK_FALSE(f.set->entries()[0].visible);
    h.send(ftxui::Event::Character(' '));
    h.send(ftxui::Event::ArrowDown);
    CHECK(f.set->focus() == 1u);
    h.send(ftxui::Event::Character('r'));
    CHECK(f.set->reference() == 1u);
    CHECK(line_with(h.frame(), "Δ −3 дБ").size() > 0);
    h.send(ftxui::Event::Delete);
    CHECK(f.set->entries().size() == 1);
    CHECK_FALSE(f.set->reference());
    h.send(ftxui::Event::Character('d'));
    CHECK(h.frame().find("[лин]") != std::string::npos);
}

TEST_CASE("filters screen in ascii mode draws no unicode graphics", "[tui][filters]") {
    Fixture f{150, 44, GlyphMode::ascii, ColorDepth::none};
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::f0}));
    f.screen->set_cursor_a(0.5);
    const auto t = Harness{f.screen, 150, 41}.frame();
    for (const char* g : {"⣿", "━", "╍", "◉", "✓", "╭", "▸"}) {
        INFO(g);
        CHECK(t.find(g) == std::string::npos);
    }
}

TEST_CASE("filters screen snapshots", "[tui][filters][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        Fixture f{w, h};
        for (const auto& r : {FilterRef{Kind::bank, "db2", FilterRole::h0}, FilterRef{Kind::bank, "db2", FilterRole::h1},
                              FilterRef{Kind::bank, "db2", FilterRole::f0}, FilterRef{Kind::equivalent, "db2", FilterRole::h0, 2, true}})
            REQUIRE(f.set->add(r));
        f.screen->set_cursor_a(0.5);
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("filters_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}
