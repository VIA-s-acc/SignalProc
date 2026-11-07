#include <catch2/catch_test_macros.hpp>
#include <cstdio>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

#include "audio/manual_sink.hpp"
#include "tui/harness.hpp"
#include "tui/shell/layout.hpp"
#include "tui/shell/overlays.hpp"
#include "core/version.hpp"
#include "tui/shell/panel.hpp"
#include "tui/shell/shell.hpp"
#include "tui/widgets/text_util.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;

namespace {

struct ShellRig {
    std::shared_ptr<UiContext> ui;
    int quits = 0;
    Harness h;

    ShellRig(int w, int h_, GlyphMode glyphs = GlyphMode::unicode, ColorDepth depth = ColorDepth::truecolor)
        : ui{signals::test::make_ui(w, h_, glyphs, depth)},
          h{make_shell(*ui, placeholder_screens(*ui), [this] { ++quits; }), w, h_} {}
};

}  // namespace

TEST_CASE("breakpoints follow the spec sizes", "[tui][shell]") {
    CHECK(classify(79, 24) == Breakpoint::tiny);
    CHECK(classify(80, 23) == Breakpoint::tiny);
    CHECK(classify(80, 24) == Breakpoint::small);
    CHECK(classify(120, 40) == Breakpoint::medium);
    CHECK(classify(150, 44) == Breakpoint::primary);
    CHECK(classify(150, 43) == Breakpoint::medium);
    CHECK(classify(180, 50) == Breakpoint::large);
    CHECK(classify(250, 70) == Breakpoint::xlarge);
}

TEST_CASE("layout sizes change only with the terminal size", "[tui][shell]") {
    PanelLayout layout{{{"left", {28, 37, 37, 42, 48}}}};
    layout.resize(150, 44);
    CHECK(layout.size("left") == 37);
    layout.size("left") = 50;
    layout.resize(150, 44);
    CHECK(layout.size("left") == 50);
    layout.resize(120, 40);
    CHECK(layout.size("left") == 37);
    layout.resize(80, 24);
    CHECK(layout.size("left") == 28);
    CHECK(layout.breakpoint() == Breakpoint::small);
    layout.toggle_maximize("spectrum");
    CHECK(layout.maximized() == "spectrum");
    layout.toggle_maximize("spectrum");
    CHECK_FALSE(layout.maximized());
    layout.next_panel(3);
    layout.next_panel(3);
    layout.next_panel(3);
    CHECK(layout.small_index() == 0);
}

TEST_CASE("digits and clicks switch tabs", "[tui][shell]") {
    ShellRig r{150, 44};
    auto t = r.h.frame();
    CHECK(t.find("1 Сигнал") != std::string::npos);
    CHECK(t.find("9 Теория") != std::string::npos);
    r.h.send(ftxui::Event::Character('3'));
    CHECK(r.h.frame().find("Фильтры") != std::string::npos);
    CHECK(active_tab(r.h.root) == 2);
    const auto first = r.h.frame();
    const auto at = first.find("1 Сигнал");
    REQUIRE(at != std::string::npos);
    const auto line_start = first.rfind('\n', at);
    const int row = static_cast<int>(std::count(first.begin(), first.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
    std::string prefix = first.substr(line_start == std::string::npos ? 0 : line_start + 1, at - (line_start == std::string::npos ? 0 : line_start + 1));
    int col = 0;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if ((static_cast<unsigned char>(prefix[i]) & 0xC0) != 0x80) ++col;
    r.h.click(col + 1, row);
    CHECK(active_tab(r.h.root) == 0);
}

TEST_CASE("help overlay opens, filters and closes", "[tui][shell]") {
    ShellRig r{150, 44};
    r.h.frame();
    r.h.send(ftxui::Event::Character('?'));
    auto open = r.h.frame();
    CHECK(open.find("вкладки") != std::string::npos);
    CHECK(open.find("выход") != std::string::npos);
    r.h.send(ftxui::Event::Character("в"));
    r.h.send(ftxui::Event::Character("ы"));
    r.h.send(ftxui::Event::Character("х"));
    auto filtered = r.h.frame();
    CHECK(filtered.find("выход") != std::string::npos);
    r.h.send(ftxui::Event::Escape);
    CHECK(r.h.frame().find("фильтр:") == std::string::npos);
}

TEST_CASE("a terminal below 80x24 shows a warning, not a layout", "[tui][shell]") {
    ShellRig r{79, 24};
    auto t = r.h.frame();
    CHECK(t.find("80×24") != std::string::npos);
    CHECK(t.find("79×24") != std::string::npos);
    CHECK(t.find("1 Сигнал") == std::string::npos);
}

TEST_CASE("status line shows the injected resources", "[tui][shell]") {
    ShellRig r{150, 44};
    auto t = r.h.frame();
    CHECK(t.find("CPU 3,1 %") != std::string::npos);
    CHECK(t.find("182 МБ") != std::string::npos);
    CHECK(t.find("8 ядер") != std::string::npos);
}

TEST_CASE("without true colour no 24-bit sequences are emitted", "[tui][shell]") {
    for (auto depth : {ColorDepth::palette256, ColorDepth::none}) {
        ShellRig r{150, 44, GlyphMode::unicode, depth};
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(150), ftxui::Dimension::Fixed(44));
        ftxui::Render(screen, r.h.root->Render());
        const auto out = screen.ToString();
        CHECK(out.find("38;2;") == std::string::npos);
        CHECK(out.find("48;2;") == std::string::npos);
    }
}

TEST_CASE("q quits", "[tui][shell]") {
    ShellRig r{150, 44};
    r.h.frame();
    r.h.send(ftxui::Event::Character('q'));
    CHECK(r.quits == 1);
}

TEST_CASE("shell snapshots", "[tui][shell][snapshot]") {
    signals::test::check_snapshot("shell_150x44", ShellRig{150, 44}.h.frame());
    signals::test::check_snapshot("shell_80x24", ShellRig{80, 24}.h.frame());
    auto ascii = ShellRig{80, 24, GlyphMode::ascii}.h.frame();
    signals::test::check_snapshot("shell_80x24_ascii", ascii);
    CHECK(ascii.find("╭") == std::string::npos);
    CHECK(ascii.find("│") == std::string::npos);
}

TEST_CASE("the theme background is painted unless colour is off", "[tui][shell]") {
    for (auto depth : {ColorDepth::truecolor, ColorDepth::none}) {
        auto ui = signals::test::make_ui(150, 44, GlyphMode::unicode, depth);
        auto shell = make_shell(*ui, placeholder_screens(*ui), [] {});
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(150), ftxui::Dimension::Fixed(44));
        ftxui::Render(screen, shell->Render());
        const auto expected = depth == ColorDepth::none ? ftxui::Color{} : to_color(ui->theme.bg, depth);
        CHECK(screen.CellAt(5, 10).background_color == expected);
        CHECK(screen.CellAt(140, 30).background_color == expected);
    }
}

TEST_CASE("jump labels are single letters up to 26, then all two letters", "[tui][menu]") {
    CHECK(jump_labels(3) == std::vector<std::string>{"a", "s", "d"});
    CHECK(jump_labels(26).back() == "m");
    const auto two = jump_labels(30);
    CHECK(two[0] == "aa");
    CHECK(two[1] == "as");
    CHECK(two[26] == "sa");
}

TEST_CASE("jump mode labels targets and a letter clicks one", "[tui][menu]") {
    ShellRig r{150, 44};
    const auto before = r.h.frame();
    r.h.send(ftxui::Event::Character('\''));
    const auto labelled = r.h.frame();
    CHECK(labelled != before);
    r.h.send(ftxui::Event::Character('d'));
    CHECK(active_tab(r.h.root) == 2);
    r.h.send(ftxui::Event::Character('\''));
    r.h.send(ftxui::Event::Character('x'));
    CHECK(active_tab(r.h.root) == 2);
    r.h.send(ftxui::Event::Character('1'));
    CHECK(active_tab(r.h.root) == 0);
}

namespace {

struct MenuScreen final : TabScreen {
    int acted = 0;
    std::string tab_label() const override { return "M"; }
    std::vector<Hint> hints() const override { return {}; }
    void set_breakpoint(Breakpoint) override {}
    ftxui::Element OnRender() override { return ftxui::text("menu screen"); }
    std::vector<MenuItem> context_menu(int, int) override {
        return {{"Первый пункт", "", [this] { ++acted; }}, {"Недоступный", "", [this] { acted += 100; }, false}};
    }
};

}  // namespace

TEST_CASE("right click opens the screen menu and an item acts once", "[tui][menu]") {
    auto ui = signals::test::make_ui(150, 44);
    auto s = std::make_shared<MenuScreen>();
    Harness h{make_shell(*ui, {s}, [] {}), 150, 44};
    h.frame();
    h.right_click(40, 20);
    const auto frame = h.frame();
    const auto at = signals::test::locate(frame, "Первый пункт");
    REQUIRE(at);
    h.click(at->first, at->second);
    CHECK(s->acted == 1);
    CHECK(h.frame().find("Первый пункт") == std::string::npos);
    h.right_click(40, 20);
    h.frame();
    const auto off = signals::test::locate(h.frame(), "Недоступный");
    REQUIRE(off);
    h.click(off->first, off->second);
    CHECK(s->acted == 1);
}

TEST_CASE("escape and keys drive the menu", "[tui][menu]") {
    auto ui = signals::test::make_ui(150, 44);
    auto s = std::make_shared<MenuScreen>();
    Harness h{make_shell(*ui, {s}, [] {}), 150, 44};
    h.frame();
    h.right_click(140, 42);
    CHECK(h.frame().find("Первый пункт") != std::string::npos);
    h.send(ftxui::Event::Escape);
    CHECK(h.frame().find("Первый пункт") == std::string::npos);
    CHECK(s->acted == 0);
    h.right_click(140, 42);
    h.frame();
    h.send(ftxui::Event::Return);
    CHECK(s->acted == 1);
}

TEST_CASE("jump mode survives redraw events and mouse moves", "[tui][menu]") {
    ShellRig r{150, 44};
    r.h.frame();
    r.h.send(ftxui::Event::Character('\''));
    r.h.send(ftxui::Event::Custom);
    ftxui::Mouse m;
    m.button = ftxui::Mouse::None;
    m.motion = ftxui::Mouse::Moved;
    m.x = 10;
    m.y = 10;
    r.h.send(ftxui::Event::Mouse("", m));
    r.h.send(ftxui::Event::Character('d'));
    CHECK(active_tab(r.h.root) == 2);
}

TEST_CASE("a lost audio device is reported once", "[tui][shell][audio]") {
    signals::test::ManualSink* raw = nullptr;
    auto player = signals::test::manual_player(raw);
    ShellRig r{150, 44};
    std::vector<std::string> warnings;
    r.ui->player = &player;
    r.ui->warn = [&](std::string line) { warnings.push_back(std::move(line)); };
    const std::vector<float> samples(1000, 0.25f);
    REQUIRE(player.play({.channels = {samples}, .rate = 8000, .owner = nullptr, .tag = "signal"}, 0));
    raw->lose("device unplugged");
    r.h.frame();
    r.h.frame();
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("device unplugged") != std::string::npos);
    CHECK(player.state() == signals::audio::PlayState::stopped);
}

namespace {

struct ManyHintsScreen final : TabScreen {
    std::string tab_label() const override { return "H"; }
    std::vector<Hint> hints() const override {
        std::vector<Hint> out;
        for (int i = 0; i < 40; ++i) out.push_back({"k" + std::to_string(i), "hint number " + std::to_string(i)});
        return out;
    }
    void set_breakpoint(Breakpoint) override {}
    ftxui::Element OnRender() override { return ftxui::text("hints screen"); }
};

}  // namespace

TEST_CASE("help is split into this screen and everywhere, fits the terminal and scrolls", "[tui][shell]") {
    auto ui = signals::test::make_ui(80, 24);
    Harness h{make_shell(*ui, {std::make_shared<ManyHintsScreen>()}, [] {}), 80, 24};
    h.frame();
    h.send(ftxui::Event::Character('?'));
    auto frame = h.frame();
    CHECK(frame.find("Этот экран") != std::string::npos);
    CHECK(frame.find("hint number 0") != std::string::npos);
    CHECK(frame.find("hint number 39") == std::string::npos);
    CHECK(frame.find("Esc — закрыть") != std::string::npos);
    CHECK(frame.find(" из ") != std::string::npos);
    h.send(ftxui::Event::End);
    frame = h.frame();
    CHECK(frame.find("hint number 0") == std::string::npos);
    CHECK(frame.find("Везде") != std::string::npos);
    CHECK(frame.find("выход") != std::string::npos);
    CHECK(frame.find("правый клик") != std::string::npos);
    h.send(ftxui::Event::Home);
    CHECK(h.frame().find("hint number 0") != std::string::npos);
}

TEST_CASE("m opens the screen menu from the keyboard", "[tui][menu]") {
    auto ui = signals::test::make_ui(150, 44);
    auto s = std::make_shared<MenuScreen>();
    Harness h{make_shell(*ui, {s}, [] {}), 150, 44};
    h.frame();
    CHECK(h.send(ftxui::Event::Character('m')));
    CHECK(h.frame().find("Первый пункт") != std::string::npos);
    h.send(ftxui::Event::Return);
    CHECK(s->acted == 1);
}

TEST_CASE("resources show what signals itself uses and fit a small terminal", "[tui][shell]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        INFO(w << "x" << h);
        ShellRig r{w, h};
        r.h.frame();
        r.h.send(ftxui::Event::Character('R'));
        auto frame = r.h.frame();
        CHECK(frame.find("Signals сейчас") != std::string::npos);
        CHECK(frame.find("Esc или R") != std::string::npos);
        std::string seen = frame;
        for (int i = 0; i < 10; ++i) {
            r.h.send(ftxui::Event::ArrowDown);
            seen += r.h.frame();
        }
        CHECK(seen.find("интерфейс") != std::string::npos);
        CHECK(seen.find("Вся система") != std::string::npos);
        r.h.send(ftxui::Event::Escape);
        CHECK(r.h.frame().find("Signals сейчас") == std::string::npos);
    }
}

TEST_CASE("panel chips that do not fit are dropped one by one and the last one stays", "[tui][shell]") {
    auto ui = signals::test::make_ui(80, 24);
    auto element = panel("Осциллограмма", {{"перетаскивание: сдвиг", false}, {"выделение", true}, {"Tab 1/6", false}}, ftxui::text("x"), true, *ui);
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(36), ftxui::Dimension::Fixed(3));
    ftxui::Render(screen, element);
    const auto top = screen.ToString().substr(0, screen.ToString().find('\n'));
    CHECK(top.find("Tab 1/6") != std::string::npos);
    CHECK(top.find("перетаскивание") == std::string::npos);
}

TEST_CASE("the tab bar shows the version and the question mark opens help", "[tui][shell]") {
    ShellRig r{150, 44};
    auto frame = r.h.frame();
    CHECK(frame.substr(0, frame.find('\n')).find(std::string{signals::core::version()}) != std::string::npos);
    const auto cells = signals::tui::split_codepoints(frame.substr(0, frame.find('\n')));
    int column = -1;
    for (std::size_t i = 0; i < cells.size(); ++i)
        if (cells[i] == "?") column = static_cast<int>(i);
    REQUIRE(column > 0);
    r.h.click(column, 0);
    CHECK(r.h.frame().find("фильтр:") != std::string::npos);
}

TEST_CASE("m7b: display_width does not count combining marks", "[tui][shell]") {
    CHECK(signals::tui::display_width("Φ̃") == 1);
    CHECK(signals::tui::display_width("x̂ · Ψ̂") == 5);
    CHECK(signals::tui::display_width("абв") == 3);
}

TEST_CASE("m7c: ellipsize measures display width, not codepoints", "[tui][shell]") {
    CHECK(signals::tui::ellipsize("Φ̃ab", 3, false) == "Φ̃ab");
    CHECK(signals::tui::ellipsize("Φ̃abcd", 3, false) == "Φ̃a…");
    CHECK(signals::tui::ellipsize("abcdef", 4, true) == "abc~");
}

TEST_CASE("wrap_words breaks at spaces and splits only words wider than the line", "[tui][text_util]") {
    using signals::tui::wrap_words;
    CHECK(wrap_words("выберите узел или провод", 14) == std::vector<std::string>{"выберите узел", "или провод"});
    CHECK(wrap_words("abcdefghij kl", 4) == std::vector<std::string>{"abcd", "efgh", "ij", "kl"});
    CHECK(wrap_words("", 10) == std::vector<std::string>{""});
    CHECK(wrap_words("a  b", 10) == std::vector<std::string>{"a  b"});
}
