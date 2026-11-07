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

TEST_CASE("phase and delay panels read every visible trace at cursor A", "[tui][filters]") {
    Fixture f;
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h1}));
    f.screen->set_cursor_a(0.5);
    const auto frame = Harness{f.screen, 150, 41}.frame();
    const auto line = line_with(frame, "A: h₀ ");
    REQUIRE_FALSE(line.empty());
    CHECK(line.find("π") != std::string::npos);
    CHECK(line.find("h₁ ") != std::string::npos);
    std::size_t count = 0;
    for (std::size_t at = line.find("A: h₀ "); at != std::string::npos; at = line.find("A: h₀ ", at + 1)) ++count;
    CHECK(count == 2);
    CHECK(frame.find("nan") == std::string::npos);
}

TEST_CASE("plot and comparison menus act on the filters screen", "[tui][filters][menu]") {
    Fixture f;
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h1}));
    Harness h{f.screen, 150, 41};
    h.frame();
    auto run = [](std::vector<MenuItem> items, const std::string& label) {
        for (auto& item : items)
            if (item.label == label) {
                REQUIRE(item.enabled);
                item.action();
                return;
            }
        FAIL("no menu item " << label);
    };
    const auto a = f.screen->area("magnitude_plot");
    run(f.screen->context_menu(column_of(*f.screen, 0.25), (a.y_min + a.y_max) / 2), "Курсор B здесь");
    REQUIRE(f.screen->cursor_b());
    CHECK(std::abs(*f.screen->cursor_b() - 0.25) <= 1.0 / (a.x_max - a.x_min + 1));
    const auto row = signals::test::locate(h.frame(), "h₁ db2 (");
    REQUIRE(row);
    run(f.screen->context_menu(row->first, row->second), "Сделать эталоном");
    CHECK(f.set->reference() == 1u);
    h.frame();
    run(f.screen->context_menu(row->first, row->second), "Удалить");
    CHECK(f.set->entries().size() == 1);
}

TEST_CASE("a long comparison name is clipped before the reference mark", "[tui][filters]") {
    Fixture f{120, 40};
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h1}));
    Harness h{f.screen, 120, 37};
    h.frame();
    h.send(ftxui::Event::Character('r'));
    const auto line = line_with(h.frame(), "эталон");
    REQUIRE_FALSE(line.empty());
    INFO(line);
    CHECK((line.find("НЧ) эталон") != std::string::npos || line.find("… эталон") != std::string::npos));
}

TEST_CASE("filters keys zoom and pan the frequency axis and move cursor b", "[tui][filters]") {
    Fixture f;
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    Harness h{f.screen, 150, 41};
    h.frame();
    CHECK(h.send(ftxui::Event::Character('+')));
    const auto [lo, hi] = f.screen->view();
    CHECK(hi - lo < 1.0);
    h.send(ftxui::Event::Character('>'));
    CHECK(f.screen->view().first > lo);
    h.send(ftxui::Event::Character('0'));
    CHECK(f.screen->view().first == 0.0);
    CHECK(f.screen->view().second == 1.0);
    h.send(ftxui::Event::ArrowRight);
    CHECK(h.send(ftxui::Event::Character('b')));
    REQUIRE(f.screen->cursor_b());
    const double b = *f.screen->cursor_b();
    h.send(ftxui::Event::ArrowRight);
    CHECK(*f.screen->cursor_b() > b);
}

TEST_CASE("the measures table keeps the reference deltas in a narrow terminal", "[tui][filters]") {
    Fixture f{120, 40};
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    REQUIRE(f.set->add({Kind::bank, "db4", FilterRole::h0}));
    Harness h{f.screen, 120, 37};
    h.frame();
    h.send(ftxui::Event::Character('r'));
    const auto frame = h.frame();
    CHECK(frame.find("Δ ") != std::string::npos);
    CHECK(frame.find("скрыто столбцов") != std::string::npos);
}

TEST_CASE("a classical IIR filter shows its poles and o, f retune it", "[tui][filters][classic]") {
    Fixture f;
    signals::test::Harness h{f.screen, 150, 41};
    REQUIRE(f.set->add(classical_ref(ClassicalSpec{})));
    f.set->set_focus(0);
    h.frame();
    auto frame = h.frame();
    CHECK(frame.find("Баттерворт ФНЧ 4") != std::string::npos);
    CHECK(frame.find("БИХ: 4 полюсов") != std::string::npos);
    h.send(ftxui::Event::Character('o'));
    h.frame();
    CHECK(f.set->entries()[0].ref.label() == "Баттерворт ФНЧ 5 · 0,25π");
    h.send(ftxui::Event::Character('f'));
    h.frame();
    CHECK(f.set->entries()[0].ref.label() == "Баттерворт ФНЧ 5 · 0,28π");
    const auto e = f.set->entries()[0];
    REQUIRE(e.analysis);
    REQUIRE(e.analysis->zp);
    CHECK(e.analysis->zp->poles.size() == 5);
}

TEST_CASE("a classical IIR filter draws its poles and shows its own properties", "[tui][filters][classic]") {
    using signals::core::BandType;
    using signals::core::IirFamily;
    Fixture f;
    signals::test::Harness h{f.screen, 150, 44};
    REQUIRE(f.set->add(classical_ref(ClassicalSpec{})));
    f.set->set_focus(0);
    h.frame();
    auto frame = h.frame();
    std::size_t crosses = 0;
    for (auto at = frame.find("×"); at != std::string::npos; at = frame.find("×", at + 1)) ++crosses;
    UNSCOPED_INFO("crosses " << crosses);
    CHECK(crosses >= 5);
    CHECK(frame.find("биортогональный") == std::string::npos);
    CHECK(frame.find("ортогональный") == std::string::npos);
    f.set->remove(0);
    REQUIRE(f.set->add(classical_ref(ClassicalSpec{ClassicalSpec::Method::iir, IirFamily::butterworth, BandType::lowpass, 20, {0.01}})));
    f.set->set_focus(0);
    h.frame();
    frame = h.frame();
    CHECK(frame.find("|H(0)| = 1") != std::string::npos);
}

namespace {

void search_and_add(Harness& h, const std::string& query) {
    h.send(ftxui::Event::Character('/'));
    h.send(ftxui::Event::Character(query));
    h.send(ftxui::Event::Return);
    h.frame();
}

}  // namespace

TEST_CASE("the spectra group adds Φ of the wavelet in focus on an 8π axis", "[tui][filters][spectra]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    REQUIRE(f.set->add({Kind::bank, "db4", FilterRole::h0}));
    f.set->set_focus(0);
    h.frame();
    search_and_add(h, "Φ");
    REQUIRE(f.set->entries().size() == 2);
    CHECK(f.set->entries()[1].ref.label() == "Φ db4");
    const auto frame = h.frame();
    CHECK(f.screen->view().second == 8);
    CHECK(frame.find("8π") != std::string::npos);
    CHECK(frame.find("нули Φ в 2πk, k ≠ 0: порядок 4") != std::string::npos);
    CHECK(frame.find("отклонение от 1") != std::string::npos);
    h.send(ftxui::Event::Character('O'));
    h.frame();
    CHECK(f.set->entries()[1].ref.label() == "Φ db4 · J=12");
    CHECK(h.frame().find("множителей в ∏ m₀(ω/2ʲ): 12") != std::string::npos);
    f.set->remove(1);
    h.frame();
    CHECK(f.screen->view().second == 1);
}

TEST_CASE("a cwt wavelet and a packet node join the comparison", "[tui][filters][spectra]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    REQUIRE(f.set->add({Kind::bank, "db2", FilterRole::h0}));
    f.set->set_focus(0);
    h.frame();
    search_and_add(h, "morl a=8");
    REQUIRE(f.set->entries().size() == 2);
    CHECK(f.set->entries()[1].ref.label() == "Ψ̂ morl a=8");
    auto frame = h.frame();
    CHECK(frame.find("Q = 3,00") != std::string::npos);
    CHECK(f.screen->view().second == 1);
    f.set->set_focus(0);
    search_and_add(h, "dad");
    REQUIRE(f.set->entries().size() == 3);
    CHECK(f.set->entries()[2].ref.label() == "dad db2");
    frame = h.frame();
    CHECK(frame.find("ИХ · dad db2") != std::string::npos);
    CHECK(frame.find("(узел пакетов)") != std::string::npos);
}

TEST_CASE("the derived groups start folded so every group header fits", "[tui][filters][spectra]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    auto frame = h.frame();
    CHECK(frame.find("▸ Спектры φ / ψ (db2)") != std::string::npos);
    CHECK(frame.find("▸ CWT: Ψ̂(aω)") != std::string::npos);
    CHECK(frame.find("Мои фильтры") != std::string::npos);
    CHECK(frame.find("morl a=2") == std::string::npos);
    const auto at = signals::test::locate(frame, "▸ CWT");
    REQUIRE(at);
    h.click(at->first + 2, at->second);
    frame = h.frame();
    CHECK(frame.find("▾ CWT: Ψ̂(aω)") != std::string::npos);
    CHECK(frame.find("morl a=2") != std::string::npos);
}
