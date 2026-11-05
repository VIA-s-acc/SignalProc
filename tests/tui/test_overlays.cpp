#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/shell/palette.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;

namespace {

void type(Harness& h, const std::vector<std::string>& chars) {
    for (const auto& c : chars) h.send(ftxui::Event::Character(c));
}

struct Rig {
    std::shared_ptr<UiContext> ui;
    Harness h;
    explicit Rig(std::shared_ptr<UiContext> u) : ui{std::move(u)}, h{make_shell(*ui, placeholder_screens(*ui), [] {}), 150, 44} { h.frame(); }
};

}  // namespace

TEST_CASE("palette filters by subsequence", "[tui][palette]") {
    std::vector<Command> all{{"theme.light", "Тема: светлая", {}}, {"theme.dark", "Тема: тёмная", {}}, {"quit", "Выход", {}}};
    auto f = filter_commands(all, "тем");
    REQUIRE(f.size() == 2);
    CHECK(filter_commands(all, "вых").front()->id == "quit");
    CHECK(filter_commands(all, "ВЫХ").front()->id == "quit");
    CHECK(filter_commands(all, "тсв").front()->id == "theme.light");
    CHECK(filter_commands(all, "zzz").empty());
    CHECK(filter_commands(all, "").size() == 3);
}

TEST_CASE("palette runs the chosen command", "[tui][palette]") {
    Rig r{signals::test::make_ui(150, 44)};
    r.h.send(ftxui::Event::Character(':'));
    CHECK(r.h.frame().find("Тема: светлая") != std::string::npos);
    type(r.h, {"с", "в", "е", "т", "л"});
    r.h.send(ftxui::Event::Return);
    CHECK(r.ui->theme.name == "light");
    CHECK(r.h.frame().find("Тема: светлая") == std::string::npos);
}

TEST_CASE("palette opens a file by path", "[tui][palette]") {
    auto ui = signals::test::make_ui(150, 44);
    std::filesystem::path opened;
    ui->open_file = [&](std::filesystem::path p) { opened = std::move(p); };
    Rig r{ui};
    r.h.send(ftxui::Event::CtrlP);
    type(r.h, {"о", "т", "к", "р"});
    r.h.send(ftxui::Event::Return);
    type(r.h, {"a", ".", "w", "a", "v"});
    r.h.send(ftxui::Event::Return);
    CHECK(opened == std::filesystem::path{"a.wav"});
}

TEST_CASE("terminal check saves preferences", "[tui][caps]") {
    auto ui = signals::test::make_ui(150, 44);
    const auto dir = signals::test::temp_path("xdg-check");
    std::filesystem::remove_all(dir);
    ui->prefs_file = dir / "signals" / "terminal.toml";
    Rig r{ui};
    r.h.send(ftxui::Event::Character(':'));
    type(r.h, {"п", "р", "о", "в", "е", "р"});
    r.h.send(ftxui::Event::Return);
    auto shown = r.h.frame();
    CHECK(shown.find("⣿") != std::string::npos);
    CHECK(shown.find("▏▎▍▌▋▊▉█") != std::string::npos);
    r.h.send(ftxui::Event::Character('3'));
    r.h.send(ftxui::Event::Character('a'));
    CHECK(r.ui->caps.glyphs == GlyphMode::ascii);
    CHECK(r.ui->caps.color == ColorDepth::palette256);
    r.h.send(ftxui::Event::Character('s'));
    auto prefs = load_prefs(ui->prefs_file);
    REQUIRE(prefs);
    CHECK(prefs->checked);
    CHECK(prefs->color == ColorDepth::palette256);
    CHECK(prefs->glyphs == GlyphMode::ascii);
}

TEST_CASE("first run opens the terminal check", "[tui][caps]") {
    auto ui = signals::test::make_ui(150, 44);
    ui->first_run_check = true;
    Rig r{ui};
    CHECK(r.h.frame().find("⣿") != std::string::npos);
}

TEST_CASE("resources panel shows history, cores, buffers and queue", "[tui][monitor]") {
    auto ui = signals::test::make_ui(150, 44);
    std::vector<ResourceSnapshot> history(300, signals::test::fixed_resources());
    for (std::size_t k = 0; k < history.size(); ++k) history[k].cpu_one_core_pct = static_cast<double>(k % 50);
    ui->history = [history] { return history; };
    std::atomic<bool> release{false}, started{false};
    JobRunner jobs{[] {}};
    jobs.submit("Открытие test.wav", {"Чтение WAV"}, [&](JobContext& c) {
        c.begin(0);
        started = true;
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    });
    while (!started) std::this_thread::sleep_for(std::chrono::milliseconds{1});
    ui->jobs = &jobs;
    Rig r{ui};
    r.h.send(ftxui::Event::Character('R'));
    auto t = r.h.frame();
    release = true;
    CHECK(t.find("Открытие test.wav") != std::string::npos);
    CHECK(t.find("МБ") != std::string::npos);
    CHECK(t.find("не учитываются") != std::string::npos);
    bool braille = false;
    for (std::size_t i = 0; i + 2 < t.size(); ++i)
        if (static_cast<unsigned char>(t[i]) == 0xE2 && (static_cast<unsigned char>(t[i + 1]) & 0xFC) == 0xA0) braille = true;
    CHECK(braille);
    r.h.send(ftxui::Event::Escape);
    CHECK(r.h.frame().find("не учитываются") == std::string::npos);
}

TEST_CASE("overlay snapshots", "[tui][snapshot]") {
    {
        Rig r{signals::test::make_ui(150, 44)};
        r.h.send(ftxui::Event::Character(':'));
        signals::test::check_snapshot("overlay_palette_150x44", r.h.frame());
    }
    {
        auto ui = signals::test::make_ui(150, 44);
        ui->first_run_check = true;
        Rig r{ui};
        signals::test::check_snapshot("overlay_check_150x44", r.h.frame());
    }
    {
        auto ui = signals::test::make_ui(150, 44);
        std::vector<ResourceSnapshot> history(300, signals::test::fixed_resources());
        for (std::size_t k = 0; k < history.size(); ++k) history[k].cpu_one_core_pct = 20.0 + static_cast<double>(k % 60);
        ui->history = [history] { return history; };
        Rig r{ui};
        r.h.send(ftxui::Event::Character('R'));
        signals::test::check_snapshot("overlay_resources_150x44", r.h.frame());
    }
}
