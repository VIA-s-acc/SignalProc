#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "i18n/i18n.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/shell/palette.hpp"
#include "tui/widgets/text_util.hpp"
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

TEST_CASE("palette also finds commands by their English id", "[tui][palette]") {
    std::vector<Command> all{{"file.open", "Открыть WAV…", {}}, {"quit", "Выход", {}}};
    const auto f = filter_commands(all, "open");
    REQUIRE(f.size() == 1);
    CHECK(f.front()->id == "file.open");
    CHECK(filter_commands(all, "откр").front()->id == "file.open");
}

TEST_CASE("palette scrolls to keep the selection visible", "[tui][palette]") {
    auto ui = signals::test::make_ui(150, 44);
    std::vector<Command> all;
    for (int i = 0; i < 30; ++i) all.push_back({"cmd." + std::to_string(i), "Команда " + std::string(i < 10 ? "0" : "") + std::to_string(i), {}});
    Palette p;
    p.open(all);
    for (int i = 0; i < 20; ++i) p.on_event(ftxui::Event::ArrowDown);
    auto text = signals::test::render_text(p.render(*ui, 80), 80, 20);
    CHECK(text.find("Команда 20") != std::string::npos);
    CHECK(text.find("Команда 00") == std::string::npos);
    CHECK(text.find("21 / 30") != std::string::npos);
    for (int i = 0; i < 20; ++i) p.on_event(ftxui::Event::ArrowUp);
    text = signals::test::render_text(p.render(*ui, 80), 80, 20);
    CHECK(text.find("Команда 00") != std::string::npos);
    p.on_event(ftxui::Event::PageDown);
    p.on_event(ftxui::Event::PageDown);
    p.on_event(ftxui::Event::PageDown);
    text = signals::test::render_text(p.render(*ui, 80), 80, 20);
    CHECK(text.find("Команда 29") != std::string::npos);
    CHECK(text.find("30 / 30") != std::string::npos);
}

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
    REQUIRE(save_prefs(ui->prefs_file, {std::nullopt, std::nullopt, false, signals::Language::en, std::nullopt}));
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
    CHECK(prefs->language == signals::Language::en);
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

TEST_CASE("closing the first-run check with Esc stops it reappearing", "[tui][caps]") {
    auto ui = signals::test::make_ui(150, 44);
    const auto dir = signals::test::temp_path("xdg-first-run");
    std::filesystem::remove_all(dir);
    ui->prefs_file = dir / "signals" / "terminal.toml";
    ui->first_run_check = true;
    Rig r{ui};
    REQUIRE(r.h.frame().find("⣿") != std::string::npos);
    r.h.send(ftxui::Event::Escape);
    const auto prefs = load_prefs(ui->prefs_file);
    REQUIRE(prefs);
    CHECK(prefs->checked);
    CHECK_FALSE(prefs->color);
    CHECK_FALSE(prefs->glyphs);
}

TEST_CASE("typed and dropped paths are expanded", "[tui][palette]") {
    const EnvLookup home = [](std::string_view k) -> std::optional<std::string> {
        return k == "HOME" ? std::optional<std::string>{"/u"} : std::nullopt;
    };
    CHECK(expand_path("  '~/My Music/a b.wav'  ", home) == std::filesystem::path{"/u/My Music/a b.wav"});
    CHECK(expand_path("\"/q/a.wav\"", home) == std::filesystem::path{"/q/a.wav"});
    CHECK(expand_path("/x/a\\ b\\(1\\).wav ", home) == std::filesystem::path{"/x/a b(1).wav"});
    CHECK(expand_path("~", home) == std::filesystem::path{"/u"});
    CHECK(expand_path("a.wav", home) == std::filesystem::path{"a.wav"});
}

TEST_CASE("the palette switches the interface language", "[tui][palette][i18n]") {
    auto ui = signals::test::make_ui(150, 44);
    std::optional<signals::Language> chosen;
    ui->switch_language = [&](signals::Language l) { chosen = l; };
    Rig r{ui};
    r.h.send(ftxui::Event::Character(':'));
    type(r.h, {"e", "n", "g", "l"});
    r.h.send(ftxui::Event::Return);
    CHECK(chosen == signals::Language::en);
}

TEST_CASE("in English the palette shows and finds English commands", "[tui][palette][i18n]") {
    const signals::LanguageScope en{signals::Language::en};
    Rig r{signals::test::make_ui(150, 44)};
    r.h.send(ftxui::Event::Character(':'));
    CHECK(r.h.frame().find("Theme: light") != std::string::npos);
    type(r.h, {"l", "i", "g", "h", "t"});
    r.h.send(ftxui::Event::Return);
    CHECK(r.ui->theme.name == "light");
}

TEST_CASE("text prompts keep a space between the prompt and the typed value", "[tui][palette]") {
    auto ui = signals::test::make_ui(150, 44);
    for (const auto& [prompt, shown] : {std::pair{std::string{"сохранить .sig как"}, std::string{"сохранить .sig как: out.sig"}},
                                        std::pair{std::string{"gain.db"}, std::string{"gain.db: out.sig"}},
                                        std::pair{std::string{"Имя банка: "}, std::string{"Имя банка: out.sig"}}}) {
        INFO(prompt);
        Palette p;
        p.ask_text(prompt, "", [](std::string) {});
        p.on_event(ftxui::Event::Character("out.sig"));
        const auto frame = signals::test::render_text(p.render(*ui, 80), 80, 6);
        CHECK(frame.find(shown) != std::string::npos);
        CHECK(frame.find("Enter — открыть") == std::string::npos);
    }
}

TEST_CASE("the palette sets the editor for sources and saves it", "[tui][palette][editor]") {
    auto ui = signals::test::make_ui(150, 44);
    const auto dir = signals::test::temp_path("xdg-editor");
    std::filesystem::remove_all(dir);
    ui->prefs_file = dir / "signals" / "terminal.toml";
    REQUIRE(save_prefs(ui->prefs_file, {std::nullopt, std::nullopt, true, signals::Language::ru, std::nullopt}));
    Rig r{ui};
    r.h.send(ftxui::Event::Character(':'));
    type(r.h, {"р", "е", "д", "а", "к", "т"});
    r.h.send(ftxui::Event::Return);
    CHECK(r.h.frame().find(signals::tr("tui.cmd.editor_prompt")) != std::string::npos);
    type(r.h, {"c", "o", "d", "e", " ", "-", "w"});
    r.h.send(ftxui::Event::Return);
    CHECK(ui->editor == "code -w");
    auto saved = load_prefs(ui->prefs_file);
    REQUIRE(saved);
    CHECK(saved->editor == std::optional<std::string>{"code -w"});
    CHECK(saved->checked);
    CHECK(saved->language == signals::Language::ru);
    r.h.send(ftxui::Event::Character(':'));
    type(r.h, {"р", "е", "д", "а", "к", "т"});
    r.h.send(ftxui::Event::Return);
    for (int k = 0; k < 10; ++k) r.h.send(ftxui::Event::Backspace);
    r.h.send(ftxui::Event::Return);
    CHECK(ui->editor.empty());
    CHECK_FALSE(load_prefs(ui->prefs_file)->editor);
}

TEST_CASE("an erased prompt answers empty, an untouched empty prompt does nothing", "[tui][palette][editor]") {
    Palette palette;
    std::optional<std::string> answer;
    palette.ask_text("name", "abc", [&](std::string v) { answer = std::move(v); });
    for (int k = 0; k < 3; ++k) palette.on_event(ftxui::Event::Backspace);
    palette.on_event(ftxui::Event::Return);
    CHECK(answer == std::optional<std::string>{""});
    answer.reset();
    palette.ask_text("path", "", [&](std::string v) { answer = std::move(v); });
    palette.on_event(ftxui::Event::Return);
    CHECK_FALSE(answer);
}

TEST_CASE("the palette opens the settings and files chapter", "[tui][palette][settings]") {
    auto ui = signals::test::make_ui(150, 44);
    std::string opened;
    ui->open_theory = [&](TheoryPage page) { opened = page.link.id; };
    Rig r{ui};
    r.h.send(ftxui::Event::Character(':'));
    for (const auto& c : split_codepoints(signals::tr("tui.cmd.settings_help"))) r.h.send(ftxui::Event::Character(c));
    r.h.send(ftxui::Event::Return);
    CHECK(opened == "settings");
}

TEST_CASE("exporting on a tab without plots says so", "[tui][palette][export]") {
    Rig r{signals::test::make_ui(150, 44)};
    r.h.send(ftxui::Event::Character(':'));
    for (const auto& c : split_codepoints(signals::tr("tui.cmd.export_plot"))) r.h.send(ftxui::Event::Character(c));
    r.h.send(ftxui::Event::Return);
    CHECK(r.h.frame().find(signals::tr("tui.export.none")) != std::string::npos);
}

