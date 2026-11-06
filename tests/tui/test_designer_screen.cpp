#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>

#include <ftxui/component/event.hpp>

#include "tui/data/user_filters.hpp"
#include "tui/harness.hpp"
#include "tui/screens/designer_screen.hpp"
#include "tui/screens/filters_model.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

struct DesignerFixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui;
    std::shared_ptr<DesignerModel> model = std::make_shared<DesignerModel>();
    std::shared_ptr<DesignerScreen> screen;
    Harness h;

    explicit DesignerFixture(int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)}, screen{(ui->jobs = &jobs, std::make_shared<DesignerScreen>(*ui, model))}, h{screen, w, hgt - 3} {
        screen->set_breakpoint(classify(w, hgt));
        model->set_p(3, jobs);
        h.frame();
    }
    void click(std::string_view id) {
        const auto b = screen->area(id);
        REQUIRE(b.x_max >= b.x_min);
        h.click(b.x_min, b.y_min);
        h.frame();
    }
};

}  // namespace

TEST_CASE("the Daubechies preset shows the canvas numbers", "[tui][designer]") {
    DesignerFixture f;
    const auto frame = f.h.frame();
    CHECK(frame.find("Результат · db3 (Добеши, минимальная фаза)") != std::string::npos);
    CHECK(frame.find("+0,3327 +0,8069 +0,4599 −0,1350 −0,0854 +0,0352") != std::string::npos);
    CHECK(frame.find("ℓ = 5 · длины 6 / 6") != std::string::npos);
    CHECK(frame.find("28 вариантов") != std::string::npos);
    CHECK(frame.find("сходится ✓") != std::string::npos);
    CHECK(frame.find("−1: 3 / 3") != std::string::npos);
    CHECK(f.screen->context().find("P₀ степени 10, p = 3") != std::string::npos);
}

TEST_CASE("clicking a root moves its group", "[tui][designer]") {
    DesignerFixture f;
    const auto before = f.model->split().group_in_h0;
    const auto hits = f.screen->root_hits();
    REQUIRE_FALSE(hits.empty());
    f.h.click(hits.front().x, hits.front().y);
    f.h.frame();
    CHECK(f.model->split().group_in_h0 != before);
    CHECK(f.h.frame().find("Результат · db3 (Добеши") == std::string::npos);
}

TEST_CASE("arrows change zeros at -1", "[tui][designer]") {
    DesignerFixture f;
    f.h.send(ftxui::Event::ArrowRight);
    CHECK(f.h.frame().find("−1: 4 / 2") != std::string::npos);
    f.h.send(ftxui::Event::ArrowLeft);
    f.h.send(ftxui::Event::ArrowLeft);
    CHECK(f.h.frame().find("−1: 2 / 4") != std::string::npos);
}

TEST_CASE("degenerate preset explains the diverging cascade", "[tui][designer]") {
    DesignerFixture f;
    f.click("preset_degenerate");
    const auto frame = f.h.frame();
    CHECK(frame.find("расходится ✗") != std::string::npos);
    CHECK(frame.find("банк фильтров без вейвлет-базиса") != std::string::npos);
}

TEST_CASE("n walks splits that pass the constraints", "[tui][designer]") {
    DesignerFixture f;
    f.click("orthogonal");
    CHECK(f.model->constraints().orthogonal);
    f.h.send(ftxui::Event::Character('n'));
    CHECK(f.h.frame().find("зеркальный db3") != std::string::npos);
    f.h.send(ftxui::Event::Character('n'));
    CHECK(f.h.frame().find("Результат · db3 (Добеши") != std::string::npos);
}

TEST_CASE("c steps the cascade iterations", "[tui][designer]") {
    DesignerFixture f;
    CHECK(f.h.frame().find("φ — каскад по H₀, 8 итераций") != std::string::npos);
    f.h.send(ftxui::Event::Character('c'));
    CHECK(f.h.frame().find("φ — каскад по H₀, 2 итерации") != std::string::npos);
}

TEST_CASE("enter saves the bank and the filters library lists it", "[tui][designer][user_filters]") {
    DesignerFixture f;
    const auto dir = std::filesystem::temp_directory_path() / "signals-designer-save";
    std::filesystem::remove_all(dir);
    f.ui->user_filters = dir;
    set_user_banks({});
    std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
    Harness shell{make_shell(*f.ui, screens, [] {}), 150, 44};
    shell.frame();
    shell.send(ftxui::Event::Return);
    CHECK(shell.frame().find("Имя банка") != std::string::npos);
    shell.send(ftxui::Event::Character('x'));
    shell.send(ftxui::Event::Return);
    CHECK(std::filesystem::exists(dir / "p3-db3x.flt"));
    REQUIRE(user_banks().size() == 1);
    CHECK(user_banks()[0].name == "p3-db3x");
    CHECK(shell.frame().find("сохранено") != std::string::npos);
    set_user_banks({});
}

TEST_CASE("larger p keeps every root inside the z-plane", "[tui][designer]") {
    DesignerFixture f;
    for (int i = 0; i < 3; ++i) f.h.send(ftxui::Event::Character('+'));
    CHECK(f.model->p() == 6);
    f.h.frame();
    const auto area = f.screen->area("roots");
    const auto hits = f.screen->root_hits();
    CHECK(hits.size() == 10);
    for (const auto& hit : hits) {
        CHECK(hit.x >= area.x_min);
        CHECK(hit.x <= area.x_max);
        CHECK(hit.y >= area.y_min);
        CHECK(hit.y <= area.y_max);
    }
}

TEST_CASE("designer snapshots", "[tui][designer][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        DesignerFixture f{w, h};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("designer_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("the selected preset keeps both brackets at every size", "[tui][designer]") {
    const std::pair<Preset, std::string> all[]{{Preset::daubechies, "Добеши"}, {Preset::max_phase, "макс. фаза"}, {Preset::cdf, "CDF"},
                                               {Preset::degenerate, "вырожд."}};
    for (const auto& [w, hgt] : {std::pair{150, 44}, std::pair{120, 40}, std::pair{180, 50}}) {
        DesignerFixture f{w, hgt};
        for (const auto& [preset, label] : all) {
            f.model->preset(preset, f.jobs);
            const auto frame = f.h.frame();
            INFO(w << "x" << hgt << " " << label);
            CHECK(frame.find("[" + label + "]") != std::string::npos);
        }
    }
}

TEST_CASE("z shows the focused designer panel on the whole screen and Esc restores", "[tui][designer][zoom]") {
    DesignerFixture f;
    for (int i = 0; i < 3; ++i) f.h.send(ftxui::Event::Tab);
    CHECK(f.h.send(ftxui::Event::Character('z')));
    auto frame = f.h.frame();
    CHECK(frame.find("АЧХ H₀") != std::string::npos);
    CHECK(frame.find("Разбиение") == std::string::npos);
    CHECK(f.h.send(ftxui::Event::Escape));
    frame = f.h.frame();
    CHECK(frame.find("Разбиение") != std::string::npos);
}

TEST_CASE("the wheel zooms designer plots at the mouse and the menu resets them", "[tui][designer][zoom]") {
    constexpr double pi = std::numbers::pi;
    DesignerFixture f;
    const auto b = f.screen->area("magnitude");
    REQUIRE(b.x_max > b.x_min);
    const int x = (b.x_min + b.x_max) / 2, y = (b.y_min + b.y_max) / 2;
    CHECK(f.h.wheel(x, y, true));
    f.h.frame();
    auto a = f.screen->plot_axis("magnitude");
    CHECK(a.lo > 0.05 * pi);
    CHECK(a.hi < 0.95 * pi);
    auto items = f.screen->context_menu(x, y);
    REQUIRE_FALSE(items.empty());
    CHECK(items.back().label == "Сбросить масштаб");
    items.back().action();
    f.h.frame();
    a = f.screen->plot_axis("magnitude");
    CHECK(a.lo == 0);
    CHECK(a.hi == Catch::Approx(pi));

    const auto c = f.screen->area("phi");
    REQUIRE(c.x_max > c.x_min);
    const auto before = f.screen->plot_axis("phi");
    CHECK(f.h.wheel((c.x_min + c.x_max) / 2, (c.y_min + c.y_max) / 2, true));
    f.h.frame();
    const auto after = f.screen->plot_axis("phi");
    CHECK(after.hi - after.lo < before.hi - before.lo);

    const auto r = f.screen->area("roots");
    const auto wide = f.screen->plot_axis("roots");
    CHECK(f.h.wheel((r.x_min + r.x_max) / 2, (r.y_min + r.y_max) / 2, true));
    f.h.frame();
    CHECK(f.screen->plot_axis("roots").hi < wide.hi);
}

TEST_CASE("dragging a zoomed designer plot pans it", "[tui][designer][zoom]") {
    DesignerFixture f;
    const auto b = f.screen->area("magnitude");
    const int x = (b.x_min + b.x_max) / 2, y = (b.y_min + b.y_max) / 2;
    f.h.wheel(x, y, true);
    f.h.wheel(x, y, true);
    f.h.frame();
    const auto before = f.screen->plot_axis("magnitude");
    CHECK(f.h.drag(x, y, x - 10, y));
    f.h.frame();
    const auto after = f.screen->plot_axis("magnitude");
    CHECK(after.lo > before.lo);
    CHECK(after.hi - after.lo == Catch::Approx(before.hi - before.lo));
}
