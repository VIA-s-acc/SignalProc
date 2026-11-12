#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <optional>

#include <chrono>
#include <memory>
#include <string>

#include <ftxui/component/event.hpp>

#include "tui/harness.hpp"
#include "tui/screens/matrix_screen.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"

using namespace signals::tui;
using signals::test::Harness;
using namespace std::chrono_literals;

namespace {

struct MatrixFixture {
    JobRunner jobs{[] {}, 0ms, true};
    std::shared_ptr<UiContext> ui;
    std::chrono::steady_clock::time_point clock{};
    std::shared_ptr<MatrixModel> model = std::make_shared<MatrixModel>();
    std::shared_ptr<MatrixScreen> screen;
    Harness h;

    explicit MatrixFixture(int w = 150, int hgt = 44)
        : ui{signals::test::make_ui(w, hgt)}, screen{(ui->jobs = &jobs, std::make_shared<MatrixScreen>(*ui, model))}, h{screen, w, hgt - 3} {
        ui->now = [this] { return clock; };
        screen->set_breakpoint(classify(w, hgt));
        h.frame();
    }
};

}  // namespace

TEST_CASE("the matrix screen shows row 5 and its dot product", "[tui][matrix]") {
    MatrixFixture f;
    const auto frame = f.h.frame();
    CHECK(frame.find("A = [L; B] · 32 × 32 · строка 5 подсвечена") != std::string::npos);
    CHECK(frame.find("L₅ · строка ФНЧ h₀") != std::string::npos);
    CHECK(frame.find("= a₅") != std::string::npos);
    CHECK(frame.find("‖AᵀA − I‖") != std::string::npos);
    CHECK(frame.find("Откуда берётся A") != std::string::npos);
    CHECK(f.screen->context().find("db2 · N = 32 · periodization") != std::string::npos);
}

TEST_CASE("arrows move into the detail block", "[tui][matrix]") {
    MatrixFixture f;
    for (int i = 0; i < 11; ++i) f.h.send(ftxui::Event::ArrowDown);
    CHECK(f.screen->row() == 16);
    CHECK(f.h.frame().find("B₀ · строка ФВЧ h₁") != std::string::npos);
    for (int i = 0; i < 40; ++i) f.h.send(ftxui::Event::ArrowUp);
    CHECK(f.screen->row() == 0);
}

TEST_CASE("b cycles extensions and warns for symmetric", "[tui][matrix]") {
    MatrixFixture f;
    f.h.send(ftxui::Event::Character('b'));
    CHECK(f.model->options().mode == signals::core::Extension::symmetric);
    CHECK(f.h.frame().find("квадратной ортогональной") != std::string::npos);
}

TEST_CASE("t shows the Gram matrix", "[tui][matrix]") {
    MatrixFixture f;
    f.h.send(ftxui::Event::Character('t'));
    CHECK(f.h.frame().find("AᵀA · 32 × 32") != std::string::npos);
}

TEST_CASE("numbers appear only when they fit", "[tui][matrix]") {
    MatrixFixture f;
    f.h.send(ftxui::Event::Character('c'));
    CHECK(f.h.frame().find("числа — при N ≤") != std::string::npos);
    f.screen->open_with(NavArgs{.wavelet = "db2", .n = 8});
    f.h.frame();
    CHECK(f.screen->numbers());
    CHECK(f.h.frame().find("+0,48") != std::string::npos);
}

TEST_CASE("clicking a cell reports its value", "[tui][matrix]") {
    MatrixFixture f;
    const auto a = f.screen->area("matrix");
    REQUIRE(a.x_max > a.x_min);
    f.h.click(a.x_min + 2 * 11, a.y_min + 2);
    CHECK(f.screen->row() == 4);
    CHECK(f.h.frame().find("A[4, 11] =") != std::string::npos);
}

TEST_CASE("space animates the row by the clock", "[tui][matrix]") {
    MatrixFixture f;
    f.h.send(ftxui::Event::Character(' '));
    f.clock += 600ms;
    f.h.frame();
    CHECK(f.screen->row() == 6);
    f.h.send(ftxui::Event::Character(' '));
    f.clock += 2s;
    f.h.frame();
    CHECK(f.screen->row() == 6);
}

TEST_CASE("open_with sets the bank and N", "[tui][matrix]") {
    MatrixFixture f;
    f.screen->open_with(NavArgs{.wavelet = "haar", .n = 8});
    CHECK(f.h.frame().find("8 × 8") != std::string::npos);
    CHECK(f.model->options().wavelet == "haar");
}

TEST_CASE("matrix snapshots", "[tui][matrix][snapshot]") {
    for (auto [w, h] : {std::pair{150, 44}, std::pair{80, 24}}) {
        MatrixFixture f{w, h};
        std::vector<std::shared_ptr<TabScreen>> screens{f.screen};
        Harness shell{make_shell(*f.ui, screens, [] {}), w, h};
        signals::test::check_snapshot("matrix_" + std::to_string(w) + "x" + std::to_string(h), shell.frame());
    }
}

TEST_CASE("z shows the focused matrix panel on the whole screen", "[tui][matrix][zoom]") {
    MatrixFixture f;
    CHECK(f.h.send(ftxui::Event::Character('z')));
    auto frame = f.h.frame();
    CHECK(frame.find("A = [L; B]") != std::string::npos);
    CHECK(frame.find("Что показать") == std::string::npos);
    CHECK(f.h.send(ftxui::Event::Escape));
    CHECK(f.h.frame().find("Что показать") != std::string::npos);
}

TEST_CASE("the wheel zooms into a block of the matrix and the menu resets it", "[tui][matrix][zoom]") {
    MatrixFixture f;
    f.model->set({.n = 128}, f.jobs);
    f.h.frame();
    auto a = f.screen->area("matrix");
    REQUIRE(a.x_max > a.x_min);
    const int x = (a.x_min + a.x_max) / 2, y = (a.y_min + a.y_max) / 2;
    for (int i = 0; i < 6; ++i) CHECK(f.h.wheel(x, y, true));
    CHECK(f.h.frame().find("участок") != std::string::npos);
    const auto b = f.screen->block();
    CHECK(b.cols < 128);
    CHECK(b.rows < 128);
    CHECK(b.c0 > 0);
    CHECK(b.r0 > 0);
    a = f.screen->area("matrix");
    f.h.click(a.x_min, a.y_min);
    CHECK(f.screen->row() == b.r0);
    auto items = f.screen->context_menu(x, y);
    const auto reset = std::find_if(items.begin(), items.end(), [](const MenuItem& m) { return m.label == signals::tr("tui.menu.reset_zoom"); });
    REQUIRE(reset != items.end());
    reset->action();
    f.h.frame();
    CHECK(f.screen->block().cols == 128);
    CHECK(f.h.frame().find("участок") == std::string::npos);
}

TEST_CASE("matrix keys change N, walk rows and columns, cycle the operation and zoom", "[tui][matrix]") {
    MatrixFixture f;
    const auto n = f.model->options().n;
    CHECK(f.h.send(ftxui::Event::Character('n')));
    CHECK(f.model->options().n == n + 2);
    f.h.send(ftxui::Event::Character('N'));
    CHECK(f.model->options().n == n);
    f.h.frame();
    f.h.send(ftxui::Event::End);
    CHECK(f.screen->row() == static_cast<std::size_t>(f.model->data()->m.rows() - 1));
    f.h.send(ftxui::Event::Home);
    CHECK(f.screen->row() == 0);
    f.h.send(ftxui::Event::PageDown);
    CHECK(f.screen->row() > 1);
    f.h.send(ftxui::Event::Home);
    f.h.send(ftxui::Event::ArrowRight);
    CHECK(f.h.frame().find("A[0, 1]") != std::string::npos);
    const auto op = f.model->options().op;
    CHECK(f.h.send(ftxui::Event::Character('v')));
    CHECK(f.model->options().op != op);
    f.h.send(ftxui::Event::Character('V'));
    CHECK(f.model->options().op == op);
    f.h.frame();
    const auto full = f.screen->block();
    CHECK(f.h.send(ftxui::Event::Character('+')));
    f.h.frame();
    CHECK(f.screen->block().rows < full.rows);
    f.h.send(ftxui::Event::End);
    f.h.frame();
    const auto b = f.screen->block();
    CHECK(f.screen->row() >= b.r0);
    CHECK(f.screen->row() < b.r0 + b.rows);
    f.h.send(ftxui::Event::Character('0'));
    f.h.frame();
    CHECK(f.screen->block().rows == full.rows);
}

TEST_CASE("s shows the singular spectrum and v reaches toeplitz plus hankel", "[tui][matrix][toeplitz_hankel]") {
    MatrixFixture f;
    f.h.frame();
    f.h.send(ftxui::Event::Character('s'));
    auto frame = f.h.frame();
    CHECK(frame.find("σ") != std::string::npos);
    CHECK(frame.find("Сингулярные числа") != std::string::npos);
    f.h.send(ftxui::Event::Character('s'));
    for (int k = 0; k < 5 && f.model->options().op != MatrixOp::toeplitz_hankel; ++k) f.h.send(ftxui::Event::Character('v'));
    REQUIRE(f.model->options().op == MatrixOp::toeplitz_hankel);
    frame = f.h.frame();
    CHECK(frame.find("C(T+H)Cᵀ") != std::string::npos);
    CHECK(frame.find("Левинсон") != std::string::npos);
}

TEST_CASE("m7b: the singular spectrum names the matrix whose values it shows", "[tui][matrix]") {
    MatrixFixture f;
    f.h.send(ftxui::Event::Character('t'));
    f.h.send(ftxui::Event::Character('s'));
    f.h.frame();
    const auto frame = f.h.frame();
    CHECK(frame.find("σᵢ · A = [L; B]") != std::string::npos);
    CHECK(frame.find("σᵢ · AᵀA") == std::string::npos);
}

TEST_CASE("m7b: the matrix card does not call square matrices non-square", "[tui][matrix][toeplitz_hankel]") {
    MatrixFixture f;
    for (int k = 0; k < 6; ++k) {
        f.h.frame();
        const auto op = f.model->options().op;
        if (op == MatrixOp::circulant || op == MatrixOp::toeplitz_hankel) {
            INFO(static_cast<int>(op));
            signals::theory::LiveValues v;
            f.screen->publish(v);
            const auto* check = v.find("check.ata_identity");
            REQUIRE(check);
            CHECK(check->text != signals::tr("tui.theory.value.not_square"));
        }
        f.h.send(ftxui::Event::Character('v'));
    }
}

TEST_CASE("the singular spectrum zooms with the wheel and resets with 0", "[tui][matrix][panels]") {
    MatrixFixture f;
    f.h.send(ftxui::Event::Character('s'));
    f.h.frame();
    const auto b = f.screen->area("sigma_plot");
    REQUIRE(b.x_max > b.x_min);
    const auto full = f.screen->plot_view("sigma");
    REQUIRE(full);
    ftxui::Mouse m;
    m.button = ftxui::Mouse::WheelUp;
    m.motion = ftxui::Mouse::Pressed;
    m.x = b.x_min + (b.x_max - b.x_min) / 3;
    m.y = (b.y_min + b.y_max) / 2;
    f.h.send(ftxui::Event::Mouse("", m));
    f.h.send(ftxui::Event::Mouse("", m));
    f.h.frame();
    const auto zoomed = f.screen->plot_view("sigma");
    REQUIRE(zoomed);
    CHECK(zoomed->hi - zoomed->lo < 0.7 * (full->hi - full->lo));
    CHECK_FALSE(f.screen->context_menu(m.x, m.y).empty());
    f.h.send(ftxui::Event::Character('0'));
    f.h.frame();
    CHECK(f.screen->plot_view("sigma")->hi - f.screen->plot_view("sigma")->lo == full->hi - full->lo);
}

TEST_CASE("the origin panel scrolls with the wheel when its text does not fit", "[tui][matrix][scroll]") {
    MatrixFixture f{120, 40};
    auto frame = f.h.frame();
    const auto box = f.screen->area("origin_panel");
    REQUIRE(box.x_max > box.x_min);
    CHECK(frame.find("Левинсон — O(N²)") == std::string::npos);
    for (int k = 0; k < 20; ++k) f.h.wheel((box.x_min + box.x_max) / 2, (box.y_min + box.y_max) / 2, false);
    frame = f.h.frame();
    CHECK(frame.find("Левинсон — O(N²)") != std::string::npos);
}

TEST_CASE("hovering a matrix control explains the hovered value", "[tui][matrix][explain]") {
    MatrixFixture f;
    auto at = [&](std::string_view id) {
        const auto b = f.screen->area(id);
        return f.screen->explain_at((b.x_min + b.x_max) / 2, (b.y_min + b.y_max) / 2);
    };
    auto over = [&](const std::string& frame, const std::string& needle) -> std::optional<std::string> {
        const auto p = signals::test::locate(frame, needle);
        return p ? f.screen->explain_at(p->first, p->second) : std::nullopt;
    };
    const auto frame = f.h.frame();
    CHECK(at("op_multilevel") == signals::tr("tui.explain.matrix.op.multilevel"));
    CHECK(at("op_toeplitz_hankel") == signals::tr("tui.explain.matrix.op.toeplitz_hankel"));
    CHECK(at("mode_symm.") == signals::tr("tui.explain.matrix.mode.symm"));
    CHECK(at("mode_zero") == signals::tr("tui.explain.matrix.mode.zero"));
    CHECK(at("order_interleaved") == signals::tr("tui.explain.matrix.order.interleaved"));
    CHECK(at("cell_mode") == signals::tr("tui.explain.matrix.cell"));
    CHECK(over(frame, signals::tr("tui.matrix.edges")) == signals::tr("tui.explain.matrix.mode"));
    CHECK(over(frame, signals::tr("tui.matrix.controls")) == signals::tr("tui.explain.matrix.panel.controls"));
    CHECK(over(frame, "‖AᵀA − I‖") == signals::tr("tui.explain.matrix.check.orth"));
    const signals::LanguageScope en{signals::Language::en};
    CHECK(at("op_multilevel") == signals::tr("tui.explain.matrix.op.multilevel"));
    CHECK(at("op_multilevel") != signals::tr_opt("tui.explain.matrix.op.analysis"));
}
