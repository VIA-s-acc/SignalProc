#include <catch2/catch_test_macros.hpp>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/string.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "core/design/families.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/diagram_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"
#include "tui/widgets/text_util.hpp"

using namespace signals::tui;
using signals::test::Harness;

namespace {

struct Fixture {
    std::shared_ptr<UiContext> ui;
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    std::shared_ptr<DesignerModel> designer = std::make_shared<DesignerModel>();
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::shared_ptr<DiagramScreen> screen;

    explicit Fixture(int w = 150, int h = 44, GlyphMode glyphs = GlyphMode::unicode, ColorDepth depth = ColorDepth::truecolor)
        : ui{signals::test::make_ui(w, h, glyphs, depth)} {
        ui->jobs = &jobs;
        designer->set_p(2, jobs);
        screen = std::make_shared<DiagramScreen>(*ui, designer, signal);
        screen->set_breakpoint(classify(w, h));
    }
};

void click(Harness& h, const DiagramScreen& s, const std::string& id) {
    const auto b = s.area(id);
    REQUIRE(b.x_max >= b.x_min);
    h.click((b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2);
}

int widest(const std::string& frame) {
    std::istringstream in{frame};
    int w = 0;
    for (std::string line; std::getline(in, line);) w = std::max(w, ftxui::string_width(line));
    return w;
}

}  // namespace

TEST_CASE("the diagram runs the designer bank and probes a clicked wire", "[tui][diagram]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    auto frame = h.frame();
    CHECK(frame.find("Конструктор") != std::string::npos);
    CHECK(frame.find("ℓ = ") != std::string::npos);
    CHECK(frame.find("✓") != std::string::npos);
    click(h, *f.screen, "d");
    CHECK(f.screen->probe() == "d");
    frame = h.frame();
    CHECK(frame.find("Щуп · d") != std::string::npos);
    click(h, *f.screen, "H0");
    CHECK(f.screen->probe() == "y0");
    CHECK(h.frame().find("Щуп · y₀") != std::string::npos);
}

TEST_CASE("P redraws the bank in polyphase form with half the multiplications", "[tui][diagram]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('P'));
    const auto frame = h.frame();
    CHECK(f.screen->polyphase());
    CHECK(frame.find("E(z)") != std::string::npos);
    CHECK(frame.find("R(z)") != std::string::npos);
    CHECK(frame.find("умножений на отсчёт") != std::string::npos);
    click(h, *f.screen, "xo");
    CHECK(f.screen->probe() == "xo");
    CHECK(h.frame().find("Щуп · xₒ · 8 отсч.") != std::string::npos);
}

TEST_CASE("the probe follows a bank change", "[tui][diagram]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    click(h, *f.screen, "y0");
    h.send(ftxui::Event::Character('w'));
    auto frame = h.frame();
    CHECK(f.screen->bank_name() == "haar");
    CHECK(frame.find("Щуп · y₀ · 17 отсч.") != std::string::npos);
    h.send(ftxui::Event::Character('w'));
    frame = h.frame();
    CHECK(f.screen->bank_name() == "db2");
    CHECK(frame.find("Щуп · y₀ · 19 отсч.") != std::string::npos);
}

TEST_CASE("the probe shows time, spectrum and numbers", "[tui][diagram]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    CHECK(f.screen->probe_view() == DiagramScreen::ProbeView::time);
    h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->probe_view() == DiagramScreen::ProbeView::spectrum);
    CHECK(h.frame().find("π/2") != std::string::npos);
    h.send(ftxui::Event::Character('v'));
    CHECK(f.screen->probe_view() == DiagramScreen::ProbeView::numbers);
    const auto frame = h.frame();
    CHECK(frame.find("a[0]") != std::string::npos);
}

TEST_CASE("the input can be a fragment of the open file", "[tui][diagram]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('i'));
    CHECK(h.frame().find("файл не открыт") != std::string::npos);
    f.signal->open(signals::test::sample("test.wav"), f.jobs);
    h.send(ftxui::Event::Character('+'));
    const auto frame = h.frame();
    CHECK(f.screen->length() == 32);
    CHECK(frame.find("файл не открыт") == std::string::npos);
    const auto r = f.screen->result();
    REQUIRE(r);
    const auto& x = r->wire("x")->values;
    REQUIRE(x.size() == 32);
    CHECK(std::count_if(x.begin(), x.end(), [](double v) { return v != 0; }) > 1);
}

TEST_CASE("an 80×24 terminal keeps the diagram and the probe apart", "[tui][diagram]") {
    for (const bool poly : {false, true}) {
        Fixture f{80, 24};
        Harness h{f.screen, 80, 24};
        h.frame();
        if (poly) h.send(ftxui::Event::Character('P'));
        const auto frame = h.frame();
        CHECK(widest(frame) <= 80);
        CHECK(frame.find(poly ? "E(z)" : "F₀") != std::string::npos);
        CHECK(frame.find("⊕") != std::string::npos);
        h.send(ftxui::Event::Tab);
        const auto probe = h.frame();
        CHECK(widest(probe) <= 80);
        CHECK(probe.find("Щуп") != std::string::npos);
    }
}

TEST_CASE("the diagram in ascii mode draws no box glyphs", "[tui][diagram]") {
    Fixture f{150, 44, GlyphMode::ascii, ColorDepth::none};
    Harness h{f.screen, 150, 44};
    const auto frame = h.frame();
    for (const char* g : {"╭", "│", "─", "⊕", "▶", "⣿"}) CHECK(frame.find(g) == std::string::npos);
}

TEST_CASE("diagram screen snapshots", "[tui][diagram][snapshot]") {
    for (auto [w, hgt] : {std::pair{150, 44}, std::pair{80, 24}}) {
        Fixture f{w, hgt};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, hgt};
        signals::test::check_snapshot("diagram_" + std::to_string(w) + "x" + std::to_string(hgt), shell.frame());
        shell.send(ftxui::Event::Character('P'));
        signals::test::check_snapshot("diagram_polyphase_" + std::to_string(w) + "x" + std::to_string(hgt), shell.frame());
    }
}

TEST_CASE("the diagram follows every designer change and leaves [ ] to the sub-tabs", "[tui][diagram]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    click(h, *f.screen, "y0");
    for (const int p : {4, 2, 4, 6, 3, 5, 2, 6}) {
        f.designer->set_p(p, f.jobs);
        h.frame();
        CHECK(h.frame().find("Щуп · y₀ · " + std::to_string(16 + 2 * p - 1) + " отсч.") != std::string::npos);
    }
    for (const auto& hint : f.screen->hints()) CHECK(hint.key.find('[') == std::string::npos);
    h.send(ftxui::Event::Character('+'));
    CHECK(f.screen->length() == 32);
}

TEST_CASE("diagram panels take focus with Tab, z maximizes, and 80×24 shows one at a time", "[tui][diagram][panels]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    CHECK(f.screen->focus() == DiagramScreen::diagram_panel_id);
    h.send(ftxui::Event::Tab);
    CHECK(f.screen->focus() == DiagramScreen::probe_panel_id);
    h.send(ftxui::Event::Character('z'));
    h.frame();
    CHECK_FALSE(f.screen->area("diagram_panel").x_max > f.screen->area("diagram_panel").x_min);
    h.send(ftxui::Event::Escape);
    h.frame();
    CHECK(f.screen->area("diagram_panel").x_max > f.screen->area("diagram_panel").x_min);
    Fixture small{80, 24};
    Harness hs{small.screen, 80, 24};
    hs.frame();
    CHECK(small.screen->area("diagram_panel").x_max > small.screen->area("diagram_panel").x_min);
    CHECK_FALSE(small.screen->area("probe_panel").x_max > small.screen->area("probe_panel").x_min);
    hs.send(ftxui::Event::Tab);
    hs.frame();
    CHECK(small.screen->area("probe_panel").x_max > small.screen->area("probe_panel").x_min);
}

TEST_CASE("the probe plots zoom with the wheel and reset with 0", "[tui][diagram][panels]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    for (const char* id : {"probe_time", "probe_spectrum"}) {
        INFO(id);
        h.frame();
        const auto b = f.screen->area(std::string{id} + "_plot");
        REQUIRE(b.x_max > b.x_min);
        const auto full = f.screen->plot_view(id);
        REQUIRE(full);
        h.wheel(b.x_min + (b.x_max - b.x_min) / 3, (b.y_min + b.y_max) / 2, true);
        h.wheel(b.x_min + (b.x_max - b.x_min) / 3, (b.y_min + b.y_max) / 2, true);
        h.frame();
        CHECK(f.screen->plot_view(id)->hi - f.screen->plot_view(id)->lo < 0.7 * (full->hi - full->lo));
        CHECK_FALSE(f.screen->context_menu(b.x_min + 2, (b.y_min + b.y_max) / 2).empty());
        h.click(b.x_min + 2, (b.y_min + b.y_max) / 2);
        h.send(ftxui::Event::Character('0'));
        h.frame();
        CHECK(f.screen->plot_view(id)->hi - f.screen->plot_view(id)->lo == full->hi - full->lo);
        h.send(ftxui::Event::Character('v'));
    }
}

TEST_CASE("the numbers view scrolls through every value of a long probe", "[tui][diagram][scroll]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    for (int k = 0; k < 8 && f.screen->length() < 4096; ++k) h.send(ftxui::Event::Character('+'));
    REQUIRE(f.screen->length() == 4096);
    h.send(ftxui::Event::Character('v'));
    h.send(ftxui::Event::Character('v'));
    REQUIRE(f.screen->probe_view() == DiagramScreen::ProbeView::numbers);
    auto frame = h.frame();
    CHECK(frame.find("▼ ещё") != std::string::npos);
    const auto box = f.screen->area("probe_panel");
    for (int k = 0; k < 4; ++k) h.wheel((box.x_min + box.x_max) / 2, (box.y_min + box.y_max) / 2, false);
    frame = h.frame();
    CHECK(frame.find("▲ ещё") != std::string::npos);
    h.send(ftxui::Event::End);
    frame = h.frame();
    CHECK(frame.find("▼ ещё") == std::string::npos);
}

TEST_CASE("the diagram bank comes from a list with every wavelet and the user banks", "[tui][diagram][user_banks]") {
    const auto db2 = signals::core::wavelet("db2");
    REQUIRE(db2);
    signals::core::set_user_wavelets([db2](std::string_view name) -> std::optional<signals::core::FilterBank> {
        if (name != "my_bank") return std::nullopt;
        auto b = *db2;
        b.name = "my_bank";
        return b;
    }, [] { return std::vector<std::string>{"my_bank"}; });
    Fixture f;
    Harness h{f.screen, 150, 44};
    std::string filter;
    f.ui->open_palette = [&](std::string text) { filter = std::move(text); };
    h.frame();
    const auto commands = f.screen->commands();
    CHECK(std::any_of(commands.begin(), commands.end(), [](const Command& c) { return c.id == "diagram.bank.coif3"; }));
    const auto mine = std::find_if(commands.begin(), commands.end(), [](const Command& c) { return c.id == "diagram.bank.my_bank"; });
    REQUIRE(mine != commands.end());
    mine->run();
    h.frame();
    CHECK(f.screen->bank_name() == "my_bank");
    for (int k = 0; k < 8; ++k) h.send(ftxui::Event::Character('w'));
    h.frame();
    CHECK(f.screen->bank_name() == "my_bank");
    const auto more = f.screen->area("more_banks");
    REQUIRE(more.x_max >= more.x_min);
    h.click(more.x_min, more.y_min);
    CHECK(filter == signals::tr("tui.diagram.cmd.bank"));
    signals::core::set_user_wavelets({}, {});
}
