#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <ftxui/component/event.hpp>

#include "tui/harness.hpp"
#include "tui/shell/shell.hpp"
#include "tui/shell/subtabs.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;

namespace {

std::string line_with(const std::string& frame, const std::string& needle) {
    std::istringstream in{frame};
    for (std::string line; std::getline(in, line);)
        if (line.find(needle) != std::string::npos) return line;
    return {};
}

struct Recorder final : TabScreen {
    std::string label;
    std::optional<NavArgs> seen;
    int ran = 0;
    explicit Recorder(std::string l) : label{std::move(l)} {}
    std::string tab_label() const override { return label; }
    std::vector<Hint> hints() const override { return {{"k", "ключ " + label}}; }
    std::string context() const override { return "контекст " + label; }
    void set_breakpoint(Breakpoint) override {}
    ftxui::Element OnRender() override { return ftxui::text("экран " + label); }
    void open_with(const NavArgs& a) override { seen = a; }
    std::vector<Command> commands() override { return {{"rec." + label, "Особая команда " + label, [this] { ++ran; }}}; }
};

std::vector<std::shared_ptr<TabScreen>> nine(std::shared_ptr<TabScreen> at, std::size_t index, UiContext& ui) {
    auto s = placeholder_screens(ui);
    s[index] = std::move(at);
    return s;
}

}  // namespace

TEST_CASE("sub-tabs switch by click and brackets", "[tui][subtabs]") {
    auto ui = signals::test::make_ui(150, 44);
    auto a = std::make_shared<Recorder>("A"), b = std::make_shared<Recorder>("B");
    auto st = make_subtabs("Банк", {{"Конструктор", a}, {"Блок-схема", b}});
    Harness h{make_shell(*ui, {st}, [] {}), 150, 44};
    auto frame = h.frame();
    CHECK(line_with(frame, "[Конструктор]").find("Блок-схема") != std::string::npos);
    CHECK(line_with(frame, "[Конструктор]").find("контекст A") != std::string::npos);
    CHECK(frame.find("экран A") != std::string::npos);
    h.send(ftxui::Event::Character(']'));
    frame = h.frame();
    CHECK(line_with(frame, "[Блок-схема]").size() > 0);
    CHECK(frame.find("экран B") != std::string::npos);
    CHECK(frame.find("ключ B") != std::string::npos);
    const auto at = signals::test::locate(frame, "Конструктор");
    REQUIRE(at);
    h.click(at->first + 1, at->second);
    CHECK(h.frame().find("экран A") != std::string::npos);
    h.send(ftxui::Event::Character('['));
    CHECK(h.frame().find("экран B") != std::string::npos);
}

TEST_CASE("navigate selects the tab, the sub-tab and passes arguments", "[tui][subtabs]") {
    auto ui = signals::test::make_ui(150, 44);
    auto matrix = std::make_shared<Recorder>("M");
    auto explain = std::make_shared<Recorder>("E");
    auto theory = make_subtabs("Теория", {{"Объяснение", explain}, {"Под капотом", std::make_shared<Recorder>("U")},
                                          {"Учебник", std::make_shared<Recorder>("T")}}, 2);
    auto screens = nine(matrix, 5, *ui);
    screens[8] = theory;
    Harness h{make_shell(*ui, screens, [] {}), 150, 44};
    h.frame();
    REQUIRE(ui->navigate);
    ui->navigate("matrix", NavArgs{.wavelet = "haar", .n = 8});
    CHECK(active_tab(h.root) == 5);
    REQUIRE(matrix->seen);
    CHECK(matrix->seen->wavelet == "haar");
    CHECK(matrix->seen->n == 8u);
    ui->navigate("explain", NavArgs{.step = 9});
    CHECK(active_tab(h.root) == 8);
    REQUIRE(explain->seen);
    CHECK(explain->seen->step == 9u);
    CHECK(h.frame().find("экран E") != std::string::npos);
    ui->navigate("tab:2", {});
    CHECK(active_tab(h.root) == 1);
    ui->navigate("nowhere", {});
    CHECK(active_tab(h.root) == 1);
}

TEST_CASE("screen commands join the palette while the screen is active", "[tui][subtabs]") {
    auto ui = signals::test::make_ui(150, 44);
    auto r = std::make_shared<Recorder>("R");
    Harness h{make_shell(*ui, nine(r, 0, *ui), [] {}), 150, 44};
    h.frame();
    h.send(ftxui::Event::Character(':'));
    for (char c : std::string{"особая"}) h.send(ftxui::Event::Character(c));
    CHECK(h.frame().find("Особая команда R") != std::string::npos);
    h.send(ftxui::Event::Return);
    CHECK(r->ran == 1);
    h.send(ftxui::Event::Character('2'));
    h.send(ftxui::Event::Character(':'));
    for (char c : std::string{"особая"}) h.send(ftxui::Event::Character(c));
    CHECK(h.frame().find("Особая команда R") == std::string::npos);
}

TEST_CASE("open_palette starts with a filter", "[tui][subtabs]") {
    auto ui = signals::test::make_ui(150, 44);
    auto r = std::make_shared<Recorder>("R");
    Harness h{make_shell(*ui, nine(r, 0, *ui), [] {}), 150, 44};
    h.frame();
    REQUIRE(ui->open_palette);
    ui->open_palette("особая");
    CHECK(h.frame().find("Особая команда R") != std::string::npos);
}

TEST_CASE("ask_text returns the edited value", "[tui][subtabs]") {
    auto ui = signals::test::make_ui(150, 44);
    Harness h{make_shell(*ui, placeholder_screens(*ui), [] {}), 150, 44};
    h.frame();
    REQUIRE(ui->ask_text);
    std::optional<std::string> got;
    ui->ask_text("Имя банка", "p3", [&](std::string s) { got = std::move(s); });
    CHECK(h.frame().find("Имя банка") != std::string::npos);
    h.send(ftxui::Event::Character('x'));
    h.send(ftxui::Event::Return);
    REQUIRE(got);
    CHECK(*got == "p3x");
}
