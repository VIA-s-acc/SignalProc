#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <thread>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "support/cli_run.hpp"
#include "support/paths.hpp"
#include "tui/app_screens.hpp"
#include "tui/data/session.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/harness.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

const fs::path kPrak1 = fs::path{SIGNALS_SOURCE_DIR} / "examples" / "prak1";

struct WorkingDir {
    fs::path previous = fs::current_path();
    explicit WorkingDir(const fs::path& dir) { fs::current_path(dir); }
    ~WorkingDir() { fs::current_path(previous); }
    WorkingDir(const WorkingDir&) = delete;
    WorkingDir& operator=(const WorkingDir&) = delete;
};

struct Student {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    signals::audio::Player player = signals::test::manual_player(raw);
    std::shared_ptr<UiContext> ui;
    AppModels models = make_app_models();
    fs::path home;
    std::optional<Harness> shell;

    explicit Student(const std::string& name, int w = 150, int h = 44) : ui{signals::test::make_ui(w, h)} {
        home = signals::test::temp_path(name);
        fs::remove_all(home);
        fs::create_directories(home);
        ui->jobs = &jobs;
        ui->player = &player;
        ui->session = std::make_shared<SessionStore>(home / "session.toml");
        ui->user_filters = home / "filters";
        ui->first_run_start = true;
        const auto library = load_user_banks(ui->user_filters);
        set_user_banks(library.banks);
        install_user_wavelets();
        set_user_filters(library.filters);
        shell.emplace(make_shell(*ui, make_app_screens(*ui, models), [] {}), w, h);
    }
    ~Student() {
        set_user_filters({});
        set_user_banks({});
    }
    Student(const Student&) = delete;
    Student& operator=(const Student&) = delete;

    std::string frame() { return shell->frame(); }
    void close_welcome() {
        const auto first = frame();
        INFO(first);
        CHECK(first.find("Добро пожаловать в Signals") != std::string::npos);
        CHECK(first.find("Esc — закрыть") != std::string::npos);
        send(ftxui::Event::Escape);
        CHECK(frame().find("Добро пожаловать в Signals") == std::string::npos);
    }
    void key(char c) { shell->send(ftxui::Event::Character(c)); }
    void keys(std::string_view s) {
        for (char c : s) key(c);
    }
    void type(const std::string& text) { shell->send(ftxui::Event::Character(text)); }
    void send(const ftxui::Event& e) { shell->send(e); }
    void enter() { send(ftxui::Event::Return); }
    void erase() {
        for (int k = 0; k < 64; ++k) send(ftxui::Event::Backspace);
    }
    void retype(const std::string& text) {
        erase();
        type(text);
        enter();
    }
    void wait_run() {
        frame();
        for (int k = 0; k < 2000 && models.pipeline->run_view().running; ++k) std::this_thread::sleep_for(5ms);
    }
    bool click_on(std::string_view needle) {
        const auto at = signals::test::locate(frame(), needle);
        return at && shell->click(at->first, at->second);
    }
};

bool shows(const std::string& frame, std::string_view text) { return frame.find(text) != std::string::npos; }

bool matches(const std::string& frame, const char* pattern) { return std::regex_search(frame, std::regex{pattern}); }

}  // namespace

#define CHECK_SHOWS(frame, text)   \
    do {                           \
        INFO(frame);               \
        CHECK(shows(frame, text)); \
    } while (false)

#define CHECK_MATCHES(frame, pattern)   \
    do {                                \
        INFO(frame);                    \
        CHECK(matches(frame, pattern)); \
    } while (false)

TEST_CASE("prak1 interface: the welcome window closes with esc and comes back from the command window", "[tui][prak1]") {
    Student s{"prak1-welcome"};
    s.close_welcome();
    s.key(':');
    s.type("Стартовая панель");
    auto frame = s.frame();
    CHECK_SHOWS(frame, "Стартовая панель");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "Добро пожаловать в Signals");
    s.send(ftxui::Event::Escape);
    s.key('8');
    CHECK(active_tab(s.shell->root) == 7);
}

TEST_CASE("prak1 task 1: the ready graph in the pipeline tab", "[tui][prak1]") {
    WorkingDir cwd{kPrak1};
    Student s{"prak1-ready"};
    s.close_welcome();
    s.key('8');
    auto frame = s.frame();
    CHECK(active_tab(s.shell->root) == 7);
    CHECK_SHOWS(frame, "граф пуст: a — добавить блок, o — открыть .sig, O — примеры");
    s.key('o');
    frame = s.frame();
    CHECK_SHOWS(frame, "открыть .sig:");
    s.type("zadanie1.sig");
    s.enter();
    s.key('r');
    s.wait_run();
    frame = s.frame();
    CHECK_SHOWS(frame, "Щуп · y0 = x*h0");
    s.keys("vv");
    frame = s.frame();
    for (const auto* row : {"n  ch0", "0    1", "1  0,5", "2    1", "3    2", "4  0,5"}) {
        INFO(row);
        CHECK_SHOWS(frame, row);
    }
    s.send(ftxui::Event::Tab);
    s.send(ftxui::Event::ArrowRight);
    frame = s.frame();
    CHECK_SHOWS(frame, "Щуп · y1 = x*h1");
    for (const auto* row : {"n   ch0", "0     1", "1  −1,5", "2     2", "3    −1", "4  −0,5"}) {
        INFO(row);
        CHECK_SHOWS(frame, row);
    }
}

TEST_CASE("prak1 task 1: the graph from scratch", "[tui][prak1]") {
    Student s{"prak1-scratch"};
    WorkingDir cwd{s.home};
    s.close_welcome();
    s.key('8');
    s.key('a');
    auto frame = s.frame();
    CHECK_SHOWS(frame, "Добавить блок");
    s.type("samples");
    frame = s.frame();
    CHECK_MATCHES(frame, "Отсчёты +samples");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "samples · Отсчёты");
    CHECK_SHOWS(frame, "values=4 6 10 12");
    s.enter();
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "samples.values: 4 6 10 12 8 6 5 5");
    s.retype("2 -1 3 1");
    s.send(ftxui::Event::ArrowDown);
    s.enter();
    s.retype("1");
    s.send(ftxui::Event::Escape);
    frame = s.frame();
    CHECK_SHOWS(frame, "values=2 -1 3 1");

    s.key('c');
    s.key('a');
    s.type("pad");
    s.enter();
    s.enter();
    frame = s.frame();
    CHECK_MATCHES(frame, "◉(─)+▶○ before=0");
    CHECK_SHOWS(frame, "samples -> pad");
    s.enter();
    s.send(ftxui::Event::ArrowDown);
    s.enter();
    s.retype("1");
    s.send(ftxui::Event::Escape);
    frame = s.frame();
    CHECK_SHOWS(frame, "before=0 after=1");

    s.key('c');
    s.key('a');
    s.type("fir_taps");
    s.enter();
    s.enter();
    s.enter();
    s.enter();
    s.retype("0.5 0.5");
    s.send(ftxui::Event::Escape);
    s.key('p');
    frame = s.frame();
    CHECK_SHOWS(frame, "probe fir_taps");
    s.key('n');
    s.retype("y0 = x*h0");
    frame = s.frame();
    CHECK_SHOWS(frame, "probe fir_taps as \"y0 = x*h0\"");

    REQUIRE(s.click_on("pad ·"));
    frame = s.frame();
    CHECK_SHOWS(frame, "pad · Дополнение");
    s.key('c');
    s.key('a');
    s.type("fir_taps");
    s.enter();
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "fir_taps2");
    s.enter();
    s.enter();
    s.retype("0.5 -0.5");
    s.send(ftxui::Event::Escape);
    s.key('p');
    s.key('n');
    s.retype("y1 = x*h1");

    s.key('r');
    s.wait_run();
    s.keys("vv");
    frame = s.frame();
    CHECK_SHOWS(frame, "Щуп · y0 = x*h0");
    CHECK_SHOWS(frame, "1  0,5");
    s.send(ftxui::Event::Tab);
    s.send(ftxui::Event::ArrowRight);
    frame = s.frame();
    CHECK_SHOWS(frame, "Щуп · y1 = x*h1");
    CHECK_SHOWS(frame, "1  −1,5");

    s.send(ftxui::Event::CtrlS);
    frame = s.frame();
    CHECK_SHOWS(frame, "сохранить .sig как:");
    s.type("moy_zadanie1.sig");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "moy_zadanie1.sig сохранён");
    REQUIRE(fs::exists(s.home / "moy_zadanie1.sig"));
    auto [code, out, err] = signals::test::run_cli({"signals", "run", (s.home / "moy_zadanie1.sig").string()});
    INFO(err);
    CHECK(code == 0);
    CHECK(shows(out, "ch0: 1 0.5 1 2 0.5"));
    CHECK(shows(out, "ch0: 1 -1.5 2 -1 -0.5"));
}

TEST_CASE("prak1 tasks 2 and 4: own filters and the toeplitz matrix", "[tui][prak1]") {
    Student s{"prak1-filters"};
    s.close_welcome();
    s.key('3');
    s.key('c');
    s.type("h0 = 0.5 0.5");
    s.enter();
    auto frame = s.frame();
    CHECK_SHOWS(frame, "h0 (свой КИХ)");
    CHECK_SHOWS(frame, "сохранён как «h0» — h0.flt");
    s.key('c');
    s.type("h1 = 0.5 -0.5");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "Мои фильтры");
    CHECK(fs::exists(s.home / "filters" / "h0.flt"));
    CHECK(fs::exists(s.home / "filters" / "h1.flt"));
    frame = s.frame();
    CHECK_SHOWS(frame, "[дБ] лин");
    s.key('d');
    frame = s.frame();
    CHECK_SHOWS(frame, "дБ [лин]");
    CHECK_SHOWS(frame, "0,500π");
    CHECK_SHOWS(frame, "ИХ · h1");

    s.key('6');
    s.keys("vv");
    frame = s.frame();
    CHECK_SHOWS(frame, "Свёртка T(h)");
    for (int k = 0; k < 14; ++k) s.key('N');
    frame = s.frame();
    CHECK_MATCHES(frame, "N +4 ◂▸");
    s.key(':');
    s.type("фильтр h0");
    frame = s.frame();
    CHECK_SHOWS(frame, "Матрица: фильтр h0");
    s.enter();
    frame = s.frame();
    CHECK_MATCHES(frame, "фильтр +h0 ▾");
    s.key('c');
    frame = s.frame();
    CHECK_MATCHES(frame, "ячейка +цвет · \\[числа\\]");
    CHECK_SHOWS(frame, "T(h₀) · 5 × 4");
    CHECK_SHOWS(frame, "+0,50");
    CHECK_SHOWS(frame, "Откуда берётся A");
    s.key(':');
    s.type("фильтр h1");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "−0,50");
}

TEST_CASE("prak1 task 3: the convolution code inside the program", "[tui][prak1]") {
    Student s{"prak1-sources"};
    s.close_welcome();
    s.key('9');
    s.key(']');
    auto frame = s.frame();
    CHECK_SHOWS(frame, "[Под капотом]");
    s.key('/');
    s.type("Свёртка");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "Свёртка — прямая и через FFT");
    s.key('o');
    frame = s.frame();
    CHECK_SHOWS(frame, "Исходник · src/core/conv.cpp · convolve()");
    CHECK_SHOWS(frame, "32 │ Laurent convolve(");
    s.send(ftxui::Event::Escape);
    frame = s.frame();
    CHECK_SHOWS(frame, "Свёртка — прямая и через FFT");
    s.key('/');
    s.type("ленточная");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "Свёртка как тёплицева матрица");
    s.key('o');
    frame = s.frame();
    CHECK_SHOWS(frame, "Исходник · src/core/matrix.cpp · convolution_matrix()");
    s.send(ftxui::Event::Escape);
    s.key('/');
    s.type("АЧХ и ЛАЧХ");
    s.enter();
    frame = s.frame();
    CHECK_SHOWS(frame, "Частотная характеристика фильтра");
    s.key('o');
    frame = s.frame();
    CHECK_SHOWS(frame, "Исходник · src/core/freq.cpp · frequency_response()");
}

TEST_CASE("prak1 task 5: peaks of the two filters in the pipeline tab", "[tui][prak1]") {
    WorkingDir cwd{kPrak1};
    Student s{"prak1-peaks"};
    s.close_welcome();
    s.key('8');
    s.key('o');
    s.type("zadanie5.sig");
    s.enter();
    s.key('r');
    s.wait_run();
    s.send(ftxui::Event::Tab);
    auto frame = s.frame();
    CHECK_SHOWS(frame, "Щуп · вход x");
    for (const auto* name : {"выход ФВЧ y1", "выход ФНЧ y0", "пики x"}) {
        s.send(ftxui::Event::ArrowRight);
        frame = s.frame();
        CHECK_SHOWS(frame, std::string{"Щуп · "} + name);
    }
    CHECK_SHOWS(frame, "peak1_hz");
    CHECK_SHOWS(frame, "78,12");
    CHECK_SHOWS(frame, "−14,6");
    CHECK_SHOWS(frame, "398,4");
    CHECK_SHOWS(frame, "−23,04");
    s.send(ftxui::Event::ArrowRight);
    frame = s.frame();
    CHECK_SHOWS(frame, "Щуп · пики y0");
    CHECK_SHOWS(frame, "−14,85");
    CHECK_SHOWS(frame, "−33,24");
    s.send(ftxui::Event::ArrowRight);
    frame = s.frame();
    CHECK_SHOWS(frame, "Щуп · пики y1");
    CHECK_SHOWS(frame, "−23,48");
    CHECK_SHOWS(frame, "−27,24");
}
