#include <catch2/catch_test_macros.hpp>

#include <optional>

#include <ftxui/component/event.hpp>
#include <ftxui/screen/string.hpp>

#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "i18n/i18n.hpp"
#include "support/paths.hpp"
#include "tui/harness.hpp"
#include "tui/screens/resample_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/shell/subtabs.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::tr;
using signals::test::Harness;

namespace {

struct Fixture {
    std::shared_ptr<UiContext> ui;
    JobRunner jobs{[] {}, std::chrono::milliseconds{0}, true};
    std::shared_ptr<SignalModel> signal = std::make_shared<SignalModel>();
    std::shared_ptr<ResampleScreen> screen;

    explicit Fixture(int w = 150, int h = 44) : ui{signals::test::make_ui(w, h)} {
        ui->jobs = &jobs;
        screen = std::make_shared<ResampleScreen>(*ui, signal);
        screen->set_breakpoint(classify(w, h));
    }
};

int widest(const std::string& frame) {
    std::istringstream in{frame};
    int w = 0;
    for (std::string line; std::getline(in, line);) w = std::max(w, ftxui::string_width(line));
    return w;
}

}  // namespace

TEST_CASE("the resampling view folds a tone and reports where it lands", "[tui][resample]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    auto frame = h.frame();
    CHECK(frame.find("Ресемплинг") != std::string::npos);
    CHECK(frame.find("fs′ = 12") != std::string::npos);
    CHECK(frame.find("подавляет") != std::string::npos);
    h.send(ftxui::Event::Character('a'));
    frame = h.frame();
    CHECK(f.screen->options().filter == AntiAlias::bypass);
    CHECK(frame.find("складывается на 5") != std::string::npos);
    CHECK(frame.find("4 копии") != std::string::npos);
    h.send(ftxui::Event::Character('m'));
    frame = h.frame();
    CHECK(f.screen->options().mode == ResampleMode::up);
    CHECK(frame.find("изображений") != std::string::npos);
    REQUIRE(f.screen->theory_link());
    CHECK(f.screen->theory_link()->id == "upsample");
    h.send(ftxui::Event::Character('s'));
    h.send(ftxui::Event::Character('s'));
    CHECK(f.screen->options().source == ResampleSource::file);
    CHECK(h.frame().find("Файл не открыт") != std::string::npos);
}

TEST_CASE("the resampling view fits 80×24", "[tui][resample]") {
    Fixture f{80, 24};
    Harness h{f.screen, 80, 24};
    const auto frame = h.frame();
    CHECK(widest(frame) <= 80);
    CHECK(frame.find("До · fs") != std::string::npos);
}

TEST_CASE("the spectrum tab switches sub-tabs with braces and leaves brackets to the spectrogram", "[tui][resample]") {
    Fixture f;
    struct Probe final : TabScreen {
        int brackets = 0;
        std::string tab_label() const override { return "probe"; }
        std::vector<Hint> hints() const override { return {}; }
        void set_breakpoint(Breakpoint) override {}
        ftxui::Element OnRender() override { return ftxui::text("probe"); }
        bool OnEvent(ftxui::Event e) override {
            if (e == ftxui::Event::Character('[') || e == ftxui::Event::Character(']')) return ++brackets, true;
            return false;
        }
    };
    auto probe = std::make_shared<Probe>();
    auto tabs = make_subtabs("Спектр", {{"Спектрограмма", probe}, {"Ресемплинг", f.screen}}, 0, SubTabKeys{'{', '}'});
    Harness h{tabs, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('['));
    CHECK(probe->brackets == 1);
    CHECK(tabs->hints().front().key == "{ }");
    h.send(ftxui::Event::Character('}'));
    CHECK(h.frame().find("fs′") != std::string::npos);
}

TEST_CASE("resampling snapshots", "[tui][resample][snapshot]") {
    for (auto [w, hgt] : {std::pair{150, 44}, std::pair{80, 24}}) {
        Fixture f{w, hgt};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, hgt};
        signals::test::check_snapshot("resample_" + std::to_string(w) + "x" + std::to_string(hgt), shell.frame());
    }
}

TEST_CASE("m6i review: the file fragment follows the selection and the loudest place", "[tui][resample]") {
    Fixture f;
    f.signal->open(signals::test::sample("test.wav"), f.jobs);
    Harness h{f.screen, 150, 44};
    h.frame();
    for (int k = 0; k < 2; ++k) h.send(ftxui::Event::Character('s'));
    REQUIRE(f.screen->options().source == ResampleSource::file);
    h.frame();
    CHECK(f.screen->fragment_start() > 0);
    const auto first = f.screen->view();
    f.signal->select(1.0, 1.5);
    h.frame();
    CHECK(f.screen->fragment_start() == static_cast<std::size_t>(1.0 * f.signal->data()->info.sample_rate));
    CHECK(f.screen->view() != first);
}

TEST_CASE("m6i review: the tone readout follows the filter's real response", "[tui][resample]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('-'));
    h.send(ftxui::Event::Character('-'));
    REQUIRE(f.screen->options().m == 2);
    for (int k = 0; k < 21; ++k) h.send(ftxui::Event::Character('f'));
    REQUIRE(f.screen->options().tone_hz == 12250);
    auto frame = h.frame();
    CHECK(frame.find("лишь на") != std::string::npos);
    CHECK(frame.find("подавляет") == std::string::npos);
    h.send(ftxui::Event::Character('+'));
    h.send(ftxui::Event::Character('+'));
    for (int k = 0; k < 29; ++k) h.send(ftxui::Event::Character('F'));
    REQUIRE(f.screen->options().tone_hz == 5000);
    for (int k = 0; k < 10; ++k) h.send(ftxui::Event::Character('c'));
    frame = h.frame();
    CHECK(frame.find("ослаблен фильтром") != std::string::npos);
    h.send(ftxui::Event::Character('m'));
    frame = h.frame();
    CHECK(frame.find(tr("tui.resample.time")) != std::string::npos);
}

TEST_CASE("m7b: upsampling without the filter speaks of images and F1 follows the larger factor", "[tui][resample]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('m'));
    REQUIRE(f.screen->options().mode == ResampleMode::up);
    h.send(ftxui::Event::Character('a'));
    h.frame();
    const auto frame = h.frame();
    CHECK(frame.find("копии накладываются") == std::string::npos);
    CHECK(frame.find("изображения") != std::string::npos);
    CHECK(f.screen->theory_link()->id == "upsample");
    h.send(ftxui::Event::Character('m'));
    REQUIRE(f.screen->options().mode == ResampleMode::rational);
    for (int k = 0; k < 8 && f.screen->options().l <= f.screen->options().m; ++k) h.send(ftxui::Event::Character('>'));
    REQUIRE(f.screen->options().l > f.screen->options().m);
    CHECK(f.screen->theory_link()->id == "upsample");
}

TEST_CASE("m7b review: rational L/1 without the filter speaks of images", "[tui][resample]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    h.send(ftxui::Event::Character('m'));
    h.send(ftxui::Event::Character('m'));
    REQUIRE(f.screen->options().mode == ResampleMode::rational);
    for (int k = 0; k < 8 && f.screen->options().m > 1; ++k) h.send(ftxui::Event::Character('-'));
    REQUIRE(f.screen->options().m == 1);
    for (int k = 0; k < 8 && f.screen->options().l < 2; ++k) h.send(ftxui::Event::Character('>'));
    REQUIRE(f.screen->options().l > 1);
    h.send(ftxui::Event::Character('a'));
    h.frame();
    const auto frame = h.frame();
    CHECK(frame.find("копии накладываются") == std::string::npos);
    CHECK(frame.find("изображения") != std::string::npos);
}

TEST_CASE("resampling panels take focus with Tab and clicks, and z maximizes the focused one", "[tui][resample][panels]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    CHECK(f.screen->focus() == ResampleScreen::controls_panel);
    h.send(ftxui::Event::Tab);
    CHECK(f.screen->focus() == ResampleScreen::before_panel);
    h.send(ftxui::Event::Character('z'));
    auto frame = h.frame();
    CHECK(frame.find("До · fs") != std::string::npos);
    CHECK(frame.find("После · fs′") == std::string::npos);
    h.send(ftxui::Event::Escape);
    frame = h.frame();
    CHECK(frame.find("После · fs′") != std::string::npos);
    const auto after = f.screen->area("after_panel");
    REQUIRE(after.x_max > after.x_min);
    h.click((after.x_min + after.x_max) / 2, (after.y_min + after.y_max) / 2);
    CHECK(f.screen->focus() == ResampleScreen::after_panel);
}

TEST_CASE("at 80×24 the resampling screen shows one panel at a time and Tab switches it", "[tui][resample][panels]") {
    Fixture f{80, 24};
    Harness h{f.screen, 80, 24};
    auto frame = h.frame();
    CHECK(frame.find("До · fs") != std::string::npos);
    CHECK(frame.find("После · fs′") == std::string::npos);
    CHECK(frame.find("Tab 1/") != std::string::npos);
    h.send(ftxui::Event::Tab);
    frame = h.frame();
    CHECK(frame.find("После · fs′") != std::string::npos);
    CHECK(frame.find("До · fs") == std::string::npos);
    CHECK(widest(frame) <= 80);
}

TEST_CASE("the wheel zooms the resampling spectra and 0 resets the focused one", "[tui][resample][panels]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    h.frame();
    const auto b = f.screen->area("before_plot");
    REQUIRE(b.x_max > b.x_min);
    const auto full = f.screen->plot_view("before");
    REQUIRE(full);
    const int x = b.x_min + (b.x_max - b.x_min) / 3, y = (b.y_min + b.y_max) / 2;
    h.wheel(x, y, true);
    h.wheel(x, y, true);
    h.frame();
    const auto zoomed = f.screen->plot_view("before");
    REQUIRE(zoomed);
    CHECK(zoomed->hi - zoomed->lo < 0.7 * (full->hi - full->lo));
    CHECK_FALSE(f.screen->context_menu(x, y).empty());
    const auto m = f.screen->options().m;
    h.click(x, y);
    h.send(ftxui::Event::Character('0'));
    h.frame();
    CHECK(f.screen->plot_view("before")->hi - f.screen->plot_view("before")->lo == full->hi - full->lo);
    CHECK(f.screen->options().m == m);
}

TEST_CASE("hovering resampling controls explains mode, filter and factors", "[tui][resample][explain]") {
    Fixture f;
    Harness h{f.screen, 150, 44};
    auto at = [&](std::string_view id) {
        const auto b = f.screen->area(id);
        return f.screen->explain_at((b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2);
    };
    auto over = [&](const std::string& frame, const std::string& needle) -> std::optional<std::string> {
        const auto p = signals::test::locate(frame, needle);
        return p ? f.screen->explain_at(p->first, p->second) : std::nullopt;
    };
    const auto frame = h.frame();
    CHECK(at("mode_up") == signals::tr("tui.explain.resample.mode.up"));
    CHECK(at("filter_bypass") == signals::tr("tui.explain.resample.filter.bypass"));
    CHECK(at("m_more") == signals::tr("tui.explain.resample.m"));
    CHECK(at("source_sweep") == signals::tr("tui.explain.resample.source.sweep"));
    CHECK(over(frame, signals::tr("tui.resample.cutoff")) == signals::tr("tui.explain.resample.cutoff"));
}
