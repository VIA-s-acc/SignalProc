#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>

#include <ftxui/component/event.hpp>

#include <algorithm>
#include <chrono>
#include <complex>
#include <functional>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>

#include "tui/data/user_filters.hpp"
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
    CHECK(frame.find("БИХ: 4 полюса, 2 биквада") != std::string::npos);
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

TEST_CASE("the axis follows visible spectra, also while they are recomputed", "[tui][filters][spectra]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    REQUIRE(f.set->add({Kind::bank, "db4", FilterRole::h0}));
    f.set->set_focus(0);
    h.frame();
    search_and_add(h, "Φ");
    h.frame();
    REQUIRE(f.screen->view().second == 8);
    f.screen->set_cursor_a(2.5);
    f.ui->jobs = nullptr;
    h.send(ftxui::Event::Character('O'));
    h.frame();
    REQUIRE_FALSE(f.set->entries()[1].analysis);
    CHECK(f.screen->view().second == 8);
    REQUIRE(f.screen->cursor_a());
    CHECK(*f.screen->cursor_a() == 2.5);
    f.set->toggle(1);
    h.frame();
    CHECK(f.screen->view().second == 1);
}

TEST_CASE("c types a custom filter, adds it and saves a named one", "[tui][filters][custom]") {
    Fixture f;
    const auto dir = std::filesystem::temp_directory_path() / "signals-custom-screen";
    std::filesystem::remove_all(dir);
    f.ui->user_filters = dir;
    set_user_filters({});
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('c'));
    CHECK(f.screen->capturing_text());
    h.send(ftxui::Event::Character("abc"));
    h.send(ftxui::Event::Return);
    CHECK_FALSE(f.screen->capturing_text());
    CHECK(h.frame().find("Не число: «abc»") != std::string::npos);
    CHECK(f.set->entries().empty());
    h.send(ftxui::Event::Character('c'));
    h.send(ftxui::Event::Character("треуг = 0.25 0.5 0.25"));
    h.send(ftxui::Event::Return);
    REQUIRE(f.set->entries().size() == 1);
    CHECK(f.set->entries()[0].ref.label() == "треуг");
    CHECK(f.set->entries()[0].analysis);
    REQUIRE(user_filters().size() == 1);
    CHECK(user_filters()[0].name == "треуг");
    CHECK(std::filesystem::exists(dir));
    auto frame = h.frame();
    CHECK(frame.find("Свойства · треуг") != std::string::npos);
    CHECK(frame.find("сохранён как «треуг»") != std::string::npos);
    h.send(ftxui::Event::Character('c'));
    h.send(ftxui::Event::Character("треуг = 0.5 0.5"));
    h.send(ftxui::Event::Return);
    REQUIRE(user_filters().size() == 2);
    CHECK(h.frame().find("сохранён как «треуг-2»") != std::string::npos);
    REQUIRE(f.set->entries().size() == 2);
    f.set->remove(1);
    CHECK(frame.find("ортогональный") == std::string::npos);
    h.send(ftxui::Event::Character('c'));
    h.send(ftxui::Event::Character("z: -1 -1"));
    h.send(ftxui::Event::Return);
    REQUIRE(f.set->entries().size() == 2);
    CHECK(f.set->entries()[1].ref.label() == "свой фильтр, 3 отвода");
    CHECK(user_filters().size() == 2);
    set_user_filters({});
    std::filesystem::remove_all(dir);
}

TEST_CASE("m6h review: a custom filter in focus keeps the wavelet groups, and c shows its prompt at 80×24", "[tui][filters][custom]") {
    {
        Fixture f;
        Harness h{f.screen, 150, 44};
        h.frame();
        h.send(ftxui::Event::Character('c'));
        h.send(ftxui::Event::Character("0.25 0.5 0.25"));
        h.send(ftxui::Event::Return);
        REQUIRE(f.set->entries().size() == 1);
        const auto frame = h.frame();
        CHECK(frame.find("Эквивалентные (db2") != std::string::npos);
        CHECK(frame.find("0.25,0.5") == std::string::npos);
    }
    Fixture f{80, 24};
    Harness h{f.screen, 80, 24};
    h.frame();
    h.send(ftxui::Event::Character('c'));
    h.send(ftxui::Event::Character("abc"));
    CHECK(h.frame().find("свой фильтр: abc") != std::string::npos);
    h.send(ftxui::Event::Return);
    CHECK(h.frame().find("Не число") != std::string::npos);
}

TEST_CASE("m7b review: a failed save is reported even when the comparison is full", "[tui][filters][custom]") {
    Fixture f;
    const auto blocker = std::filesystem::temp_directory_path() / "signals-custom-blocker";
    std::filesystem::remove_all(blocker);
    { std::ofstream{blocker} << "not a directory"; }
    f.ui->user_filters = blocker / "filters";
    set_user_filters({});
    for (const char* w : {"db2", "db4", "haar", "sym4", "coif1"}) REQUIRE(f.set->add({Kind::bank, w, FilterRole::h0}));
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('c'));
    h.send(ftxui::Event::Character("тест = 0.5 0.5"));
    h.send(ftxui::Event::Return);
    CHECK(h.frame().find("Ошибка: не удалось") != std::string::npos);
    CHECK(f.set->entries().size() == 5);
    set_user_filters({});
    std::filesystem::remove_all(blocker);
}

TEST_CASE("the library scrolls when every group is open and the wheel reaches the last group", "[tui][filters][scroll]") {
    Fixture f{120, 40};
    Harness h{f.screen, 120, 37};
    auto frame = h.frame();
    for (const auto* header : {"▸ Спектры", "▸ CWT", "▸ Пакеты"}) {
        const auto at = signals::test::locate(frame, header);
        if (!at) continue;
        h.click(at->first + 2, at->second);
        frame = h.frame();
    }
    CHECK(frame.find("▼") != std::string::npos);
    const auto box = f.screen->area("library");
    REQUIRE(box.x_max > box.x_min);
    for (int k = 0; k < 30; ++k) h.wheel((box.x_min + box.x_max) / 2, (box.y_min + box.y_max) / 2, false);
    frame = h.frame();
    CHECK(frame.find("Мои фильтры") != std::string::npos);
    CHECK(frame.find("▲") != std::string::npos);
}

TEST_CASE("the impulse plot and the z-plane zoom with the wheel and the roots list scrolls", "[tui][filters][zoom]") {
    Fixture f;
    Harness h{f.screen, 150, 41};
    ClassicalSpec spec;
    spec.family = signals::core::IirFamily::elliptic;
    spec.order = 14;
    REQUIRE(f.set->add(classical_ref(spec)));
    f.set->set_focus(0);
    h.frame();
    auto frame = h.frame();
    REQUIRE(f.set->entries()[0].analysis);
    REQUIRE(f.set->entries()[0].analysis->zp);
    const auto imp = f.screen->area("impulse_plot");
    REQUIRE(imp.x_max > imp.x_min);
    const auto wide = f.screen->plot_view("impulse");
    REQUIRE(wide);
    CHECK(h.wheel((imp.x_min + imp.x_max) / 2, (imp.y_min + imp.y_max) / 2, true));
    h.frame();
    CHECK(f.screen->plot_view("impulse")->hi - f.screen->plot_view("impulse")->lo < wide->hi - wide->lo);
    CHECK_FALSE(f.screen->context_menu((imp.x_min + imp.x_max) / 2, (imp.y_min + imp.y_max) / 2).empty());

    const auto plane = f.screen->area("zplane");
    REQUIRE(plane.x_max > plane.x_min);
    const auto before = f.screen->plot_view("zplane");
    CHECK(h.wheel(plane.x_max - 1, (plane.y_min + plane.y_max) / 2, true));
    h.frame();
    const auto after = f.screen->plot_view("zplane");
    CHECK(after->hi - after->lo < before->hi - before->lo);
    CHECK(after->lo > before->lo);

    frame = h.frame();
    CHECK(frame.find("▼") != std::string::npos);
    const auto list = f.screen->area("roots_list");
    REQUIRE(list.x_max > list.x_min);
    for (int k = 0; k < 10; ++k) h.wheel((list.x_min + list.x_max) / 2, (list.y_min + list.y_max) / 2, false);
    CHECK(h.frame().find("▲") != std::string::npos);
}

TEST_CASE("the library scrolls from the keyboard and the search keeps the highlighted match in view", "[tui][filters][scroll]") {
    Fixture f{120, 40};
    Harness h{f.screen, 120, 37};
    auto frame = h.frame();
    for (const auto* header : {"▸ Спектры", "▸ CWT", "▸ Пакеты"}) {
        const auto at = signals::test::locate(frame, header);
        if (!at) continue;
        h.click(at->first + 2, at->second);
        frame = h.frame();
    }
    const auto box = f.screen->area("library");
    h.click(box.x_min + 2, box.y_max - 1);
    h.send(ftxui::Event::End);
    frame = h.frame();
    CHECK(frame.find("Мои фильтры") != std::string::npos);
    CHECK(frame.find("▲") != std::string::npos);
    h.send(ftxui::Event::Home);
    h.send(ftxui::Event::Character('/'));
    frame = h.frame();
    REQUIRE(frame.find("▼ ещё") != std::string::npos);
    h.send(ftxui::Event::End);
    frame = h.frame();
    CHECK(frame.find("▲ ещё") != std::string::npos);
    CHECK(frame.find("▼ ещё") == std::string::npos);
}

TEST_CASE("a new search starts with its first match in view after the library was scrolled", "[tui][filters][scroll][review]") {
    Fixture f{120, 40};
    Harness h{f.screen, 120, 37};
    auto frame = h.frame();
    for (const auto* header : {"▸ Спектры", "▸ CWT", "▸ Пакеты"}) {
        const auto at = signals::test::locate(frame, header);
        if (!at) continue;
        h.click(at->first + 2, at->second);
        frame = h.frame();
    }
    h.send(ftxui::Event::Character('/'));
    h.send(ftxui::Event::Escape);
    const auto box = f.screen->area("library");
    h.click(box.x_min + 2, box.y_max - 1);
    for (int k = 0; k < 3; ++k) h.send(ftxui::Event::PageDown);
    REQUIRE(h.frame().find("▲") != std::string::npos);
    h.send(ftxui::Event::Character('/'));
    CHECK(h.frame().find("▲ ещё") == std::string::npos);
}

TEST_CASE("hovering filters chips, roles and titles explains them", "[tui][filters][explain]") {
    Fixture f;
    Harness h{f.screen, 150, 41};
    auto at = [&](std::string_view id) {
        const auto b = f.screen->area(id);
        return f.screen->explain_at((b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2);
    };
    auto over = [&](const std::string& frame, const std::string& needle) -> std::optional<std::string> {
        const auto p = signals::test::locate(frame, needle);
        return p ? f.screen->explain_at(p->first, p->second) : std::nullopt;
    };
    const auto frame = h.frame();
    CHECK(at("role_f1") == signals::tr("tui.explain.filters.role.f1"));
    CHECK(at("group") == signals::tr("tui.explain.filters.group"));
    CHECK(over(frame, signals::tr("tui.filters.chip.log")) == signals::tr("tui.explain.filters.chip.log"));
    CHECK(over(frame, signals::tr("tui.filters.chip.normalised")) == signals::tr("tui.explain.filters.chip.normalised"));
    CHECK(over(frame, signals::tr("tui.filters.delay")) == signals::tr("tui.explain.filters.panel.delay"));
}

namespace {

void run_command(FiltersScreen& s, std::string_view id) {
    const auto commands = s.commands();
    const auto it = std::find_if(commands.begin(), commands.end(), [&](const Command& c) { return c.id == id; });
    REQUIRE(it != commands.end());
    it->run();
}

}  // namespace

TEST_CASE("b and a typed in the palette become a filter with its zeros and poles", "[tui][filters][zpk]") {
    Fixture f;
    Harness h{f.screen, 150, 41};
    h.frame();
    std::vector<std::string> answers{"0.2 0.4 0.2", "1 -0.6 0.25"};
    std::size_t asked = 0;
    f.ui->ask_text = [&](std::string, std::string, std::function<void(std::string)> done) { done(answers.at(asked++)); };
    run_command(*f.screen, "filters.from_ba");
    REQUIRE(f.set->entries().size() == 1);
    const auto e = f.set->entries()[0];
    CHECK(e.ref.kind == Kind::zpk);
    REQUIRE(e.analysis);
    REQUIRE(e.analysis->stable);
    CHECK(*e.analysis->stable);
    CHECK(e.analysis->response.magnitude.front() == Catch::Approx(0.8 / 0.65));
    CHECK(h.frame().find(e.ref.label()) != std::string::npos);
    answers = {"1 x", "1"};
    asked = 0;
    run_command(*f.screen, "filters.from_ba");
    CHECK(f.set->entries().size() == 1);
    CHECK(h.frame().find("x") != std::string::npos);
}

TEST_CASE("dragging a pole on the z-plane edits a copy of the filter and Ctrl+Z undoes it", "[tui][filters][zpk]") {
    Fixture f;
    Harness h{f.screen, 150, 41};
    REQUIRE(f.set->add(classical_ref(ClassicalSpec{})));
    f.set->request(f.jobs, FiltersScreen::kPoints);
    h.frame();
    const auto original = f.set->entries()[0];
    REQUIRE(original.analysis);
    REQUIRE(original.analysis->zpk);
    const auto from = f.screen->root_cell(true, 0);
    REQUIRE(from);
    const auto plane = f.screen->area("zplane");
    const int cx = (plane.x_min + plane.x_max) / 2, cy = (plane.y_min + plane.y_max) / 2;
    CHECK(h.drag(from->first, from->second, cx, from->second + (from->second < cy ? 1 : -1)));
    h.frame();
    auto entries = f.set->entries();
    REQUIRE(entries.size() == 2);
    CHECK(f.set->focus() == 1);
    CHECK(entries[0].ref == original.ref);
    CHECK(entries[1].ref.kind == Kind::zpk);
    REQUIRE(entries[1].analysis);
    REQUIRE(entries[1].analysis->zpk);
    const auto& moved = entries[1].analysis->zpk->p;
    REQUIRE(moved.size() == original.analysis->zpk->p.size());
    CHECK(std::abs(moved[0] - original.analysis->zpk->p[0]) > 0.05);
    CHECK(std::any_of(moved.begin(), moved.end(), [&](std::complex<double> p) { return std::abs(p - std::conj(moved[0])) < 1e-12; }));
    CHECK(h.frame().find(entries[1].ref.label()) != std::string::npos);
    h.send(ftxui::Event::CtrlZ);
    h.frame();
    CHECK(f.set->entries().size() == 1);
    const auto before = f.screen->plot_view("zplane");
    CHECK(h.wheel(cx, cy, true));
    h.frame();
    const auto zoomed = f.screen->plot_view("zplane");
    h.drag(plane.x_max - 1, plane.y_max - 1, plane.x_max - 4, plane.y_max - 1);
    h.frame();
    CHECK(f.set->entries().size() == 1);
    CHECK(f.screen->plot_view("zplane")->lo != zoomed->lo);
    CHECK(zoomed->hi - zoomed->lo < before->hi - before->lo);
}

TEST_CASE("the zeros, poles and gain of a filter can be typed in", "[tui][filters][zpk]") {
    Fixture f;
    Harness h{f.screen, 150, 41};
    REQUIRE(f.set->add(classical_ref(ClassicalSpec{})));
    f.set->request(f.jobs, FiltersScreen::kPoints);
    h.frame();
    std::vector<std::string> prefills;
    std::vector<std::string> answers{"-1, -1", "0.5∠±0.25π", "0.1"};
    std::size_t asked = 0;
    f.ui->ask_text = [&](std::string, std::string prefill, std::function<void(std::string)> done) {
        prefills.push_back(std::move(prefill));
        done(answers.at(asked++));
    };
    run_command(*f.screen, "filters.edit_roots");
    REQUIRE(prefills.size() == 3);
    CHECK(prefills[0].find("-1") != std::string::npos);
    CHECK(prefills[1].find("∠±") != std::string::npos);
    auto entries = f.set->entries();
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[1].analysis);
    REQUIRE(entries[1].analysis->zpk);
    CHECK(entries[1].analysis->zpk->z.size() == 2);
    CHECK(entries[1].analysis->zpk->k == 0.1);
    for (const auto& p : entries[1].analysis->zpk->p) CHECK(std::abs(p) == Catch::Approx(0.5));
    answers = {"-1", "0.9", "1"};
    asked = 0;
    run_command(*f.screen, "filters.edit_roots");
    entries = f.set->entries();
    CHECK(entries.size() == 2);
    REQUIRE(entries[1].analysis);
    CHECK(entries[1].analysis->zpk->p == std::vector<std::complex<double>>{{0.9, 0}});
}

TEST_CASE("long filter names keep the measure columns aligned and an edited copy reads differently", "[tui][filters][zpk]") {
    Fixture f;
    Harness h{f.screen, 150, 41};
    REQUIRE(f.set->add(classical_ref(ClassicalSpec{})));
    f.set->request(f.jobs, FiltersScreen::kPoints);
    h.frame();
    std::vector<std::string> answers{"-1, -1", "0.5∠±0.25π", "0.1"};
    std::size_t asked = 0;
    f.ui->ask_text = [&](std::string, std::string, std::function<void(std::string)> done) { done(answers.at(asked++)); };
    run_command(*f.screen, "filters.edit_roots");
    const auto frame = h.frame();
    const auto entries = f.set->entries();
    REQUIRE(entries.size() == 2);
    const auto marker = signals::tr("tui.filters.zpk.edited").substr(0, signals::tr("tui.filters.zpk.edited").find('{'));
    CHECK(entries[1].ref.label().starts_with(marker));
    const auto cells = [](const std::string& line) {
        std::vector<std::string> out;
        for (std::size_t at = 0; at < line.size();) {
            std::size_t len = 1;
            const auto c = static_cast<unsigned char>(line[at]);
            if (c >= 0xF0) len = 4;
            else if (c >= 0xE0) len = 3;
            else if (c >= 0xC0) len = 2;
            out.push_back(line.substr(at, len));
            at += len;
        }
        return out;
    };
    const auto header = signals::tr("tui.filters.col.cutoff");
    std::istringstream in{frame};
    std::optional<std::size_t> end;
    int rows = 0;
    for (std::string line; std::getline(in, line);) {
        if (!end) {
            const auto at = line.find(signals::tr("tui.filters.col.filter") + " ");
            const auto h_at = line.find(header);
            if (at != std::string::npos && h_at != std::string::npos) end = cells(line.substr(0, h_at + header.size())).size();
            continue;
        }
        const bool data = line.find("Баттерворт") != std::string::npos && line.find(" ◉") == std::string::npos;
        if (!data) continue;
        const auto c = cells(line);
        REQUIRE(c.size() > *end);
        INFO(line);
        CHECK(c[*end - 1] == "π");
        CHECK(c[*end] == " ");
        ++rows;
    }
    REQUIRE(end);
    CHECK(rows == 2);
}

TEST_CASE("a double real root made by dragging a pair onto the axis splits into a pair again", "[tui][filters][zpk]") {
    using C = std::complex<double>;
    const signals::core::Zpk zpk{{}, {C{0.5, 0}, C{0.5, 0}}, 1};
    const auto split = move_root(zpk, true, 0, C{0.3, 0.4});
    CHECK(std::count(split.p.begin(), split.p.end(), C{0.3, 0.4}) == 1);
    CHECK(std::count(split.p.begin(), split.p.end(), C{0.3, -0.4}) == 1);
    const signals::core::Zpk single{{}, {C{0.5, 0}, C{-0.2, 0}}, 1};
    const auto moved = move_root(single, true, 0, C{0.3, 0.4});
    CHECK(moved.p[0] == C{0.3, 0});
    CHECK(moved.p[1] == C{-0.2, 0});
}
