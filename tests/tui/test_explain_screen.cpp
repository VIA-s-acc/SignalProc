#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include <ftxui/component/event.hpp>

#include "core/design/families.hpp"
#include "tui/harness.hpp"
#include "tui/screens/explain_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

std::string line_with(const std::string& frame, const std::string& needle) {
    std::istringstream in{frame};
    for (std::string line; std::getline(in, line);)
        if (line.find(needle) != std::string::npos) return line;
    return {};
}

struct ExplainFixture {
    std::shared_ptr<UiContext> ui;
    std::chrono::steady_clock::time_point clock{};
    std::optional<signals::core::FilterBank> user;
    std::shared_ptr<ExplainScreen> screen;
    Harness h;
    std::string nav_target;
    NavArgs nav_args;

    explicit ExplainFixture(std::size_t step = 4, int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)},
          screen{std::make_shared<ExplainScreen>(*ui, [this] { return user; })},
          h{screen, w, hgt - 3} {
        ui->now = [this] { return clock; };
        ui->navigate = [this](std::string_view t, const NavArgs& a) {
            nav_target = std::string{t};
            nav_args = a;
        };
        screen->set_breakpoint(classify(w, hgt));
        screen->open_with(NavArgs{.step = step});
        h.frame();
    }
};

}  // namespace

TEST_CASE("step 4 marks the kept odd samples", "[tui][explain]") {
    ExplainFixture f;
    const auto frame = f.h.frame();
    CHECK(frame.find("шаг 4/12: прореживание ↓2") != std::string::npos);
    CHECK(frame.find("банк Хаара") != std::string::npos);
    CHECK(frame.find("остаются нечётные") != std::string::npos);
    CHECK(frame.find("инвариант: 8 входов → 4 + 4 коэффициента") != std::string::npos);
    CHECK(frame.find("Матрица A₁, N = 8") != std::string::npos);
    CHECK(frame.find("P₀ = F₀H₀ = ½(1 + z⁻¹)²") != std::string::npos);
    CHECK(frame.find("P₀(z) − P₀(−z) = 2z⁻¹") != std::string::npos);
}

TEST_CASE("arrows walk twelve steps and clamp", "[tui][explain]") {
    ExplainFixture f{1};
    for (int i = 0; i < 20; ++i) f.h.send(ftxui::Event::ArrowRight);
    CHECK(f.screen->step() == 12);
    CHECK(f.h.frame().find("шаг 12/12: эксперимент: d = 0") != std::string::npos);
    for (int i = 0; i < 20; ++i) f.h.send(ftxui::Event::ArrowLeft);
    CHECK(f.screen->step() == 1);
}

TEST_CASE("the invariant line is computed", "[tui][explain]") {
    ExplainFixture f{5};
    auto inv = line_with(f.h.frame(), "инвариант:");
    CHECK(inv.find("446 = 440 + 6") != std::string::npos);
    CHECK(inv.find("✓") != std::string::npos);
    f.screen->open_with(NavArgs{.step = 8});
    f.h.send(ftxui::Event::Character('e'));
    inv = line_with(f.h.frame(), "инвариант:");
    CHECK(inv.find("✗") != std::string::npos);
    f.h.send(ftxui::Event::Character('E'));
    inv = line_with(f.h.frame(), "инвариант:");
    CHECK(inv.find("✓") != std::string::npos);
}

TEST_CASE("w changes the bank and keeps the step", "[tui][explain]") {
    ExplainFixture f{3};
    f.h.send(ftxui::Event::Character('w'));
    const auto frame = f.h.frame();
    CHECK(f.screen->options().bank == ExplainBank::db2);
    CHECK(f.screen->step() == 3);
    CHECK(frame.find("банк db2") != std::string::npos);
    CHECK(line_with(frame, "индекс").find(" 10") != std::string::npos);
    f.h.send(ftxui::Event::Character('w'));
    f.h.send(ftxui::Event::Character('w'));
    CHECK(f.screen->options().bank == ExplainBank::haar);
}

TEST_CASE("p switches the downsampling phase", "[tui][explain]") {
    ExplainFixture f{4};
    f.h.send(ftxui::Event::Character('p'));
    const auto frame = f.h.frame();
    CHECK(frame.find("остаются чётные") != std::string::npos);
    CHECK(frame.find("8 входов → 5 + 5") != std::string::npos);
}

TEST_CASE("auto play advances by the clock", "[tui][explain]") {
    ExplainFixture f{2};
    f.h.send(ftxui::Event::Character(' '));
    f.clock += 1s;
    f.h.frame();
    CHECK(f.screen->step() == 2);
    f.clock += 2600ms;
    f.h.frame();
    CHECK(f.screen->step() == 3);
    f.h.send(ftxui::Event::Character(' '));
    f.clock += 10s;
    f.h.frame();
    CHECK(f.screen->step() == 3);
}

TEST_CASE("clicking a step jumps to it", "[tui][explain]") {
    ExplainFixture f{1};
    const auto b = f.screen->area("step_9");
    REQUIRE(b.x_max >= b.x_min);
    f.h.click(b.x_min + 1, b.y_min);
    CHECK(f.screen->step() == 9);
    const auto next = f.screen->area("next");
    f.h.click(next.x_min + 1, next.y_min);
    CHECK(f.screen->step() == 10);
}

TEST_CASE("enter opens the step's view with this bank", "[tui][explain]") {
    ExplainFixture f{9};
    f.h.send(ftxui::Event::Return);
    CHECK(f.nav_target == "matrix");
    CHECK(f.nav_args.wavelet == "haar");
    CHECK(f.nav_args.n == 8u);
}

TEST_CASE("long user banks are clipped and not wrapped", "[tui][explain]") {
    ExplainFixture f{3};
    f.user = *signals::core::wavelet("db10");
    for (int i = 0; i < 3; ++i) f.h.send(ftxui::Event::Character('w'));
    CHECK(f.screen->options().bank == ExplainBank::user);
    const auto frame = f.h.frame();
    const auto index = line_with(frame, "индекс");
    CHECK(index.find("…") != std::string::npos);
    CHECK(index.find(" 26") == std::string::npos);
    std::istringstream in{frame};
    bool clipped_row = false;
    for (std::string l; std::getline(in, l);)
        if (l.find("y₀") != std::string::npos && l.find("…") != std::string::npos) clipped_row = true;
    CHECK(clipped_row);
}

TEST_CASE("summing normalisation shows fractions", "[tui][explain]") {
    ExplainFixture f{5};
    f.h.send(ftxui::Event::Character('f'));
    CHECK(line_with(f.h.frame(), "‖a‖²").find("11") != std::string::npos);
}

TEST_CASE("explain snapshots", "[tui][explain][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        ExplainFixture f{4, w, h};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("explain_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}
