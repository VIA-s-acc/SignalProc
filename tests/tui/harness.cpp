#include "tui/harness.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>

namespace signals::test {
namespace {

std::string screen_text(const ftxui::Screen& screen) {
    std::string out;
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string row;
        for (int x = 0; x < screen.dimx(); ++x) {
            const auto& c = screen.CellAt(x, y).character;
            row += c.empty() ? " " : c;
        }
        while (!row.empty() && row.back() == ' ') row.pop_back();
        out += row;
        if (y + 1 < screen.dimy()) out += '\n';
    }
    return out;
}

ftxui::Event mouse(ftxui::Mouse::Button button, ftxui::Mouse::Motion motion, int x, int y, bool shift, bool ctrl) {
    ftxui::Mouse m;
    m.button = button;
    m.motion = motion;
    m.shift = shift;
    m.control = ctrl;
    m.x = x;
    m.y = y;
    return ftxui::Event::Mouse("", m);
}

}  // namespace

std::string render_text(ftxui::Element element, int width, int height) {
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, element);
    return screen_text(screen);
}

std::string Harness::frame() { return render_text(root->Render(), w, h); }

bool Harness::send(ftxui::Event event) { return root->OnEvent(event); }

bool Harness::click(int x, int y, bool shift) {
    const bool a = send(mouse(ftxui::Mouse::Left, ftxui::Mouse::Pressed, x, y, shift, false));
    const bool b = send(mouse(ftxui::Mouse::Left, ftxui::Mouse::Released, x, y, shift, false));
    return a || b;
}

bool Harness::wheel(int x, int y, bool up, bool shift) {
    return send(mouse(up ? ftxui::Mouse::WheelUp : ftxui::Mouse::WheelDown, ftxui::Mouse::Pressed, x, y, shift, false));
}

bool Harness::drag(int x0, int y0, int x1, int y1, bool ctrl) {
    bool handled = send(mouse(ftxui::Mouse::Left, ftxui::Mouse::Pressed, x0, y0, false, ctrl));
    const int steps = 4;
    for (int k = 1; k <= steps; ++k)
        handled = send(mouse(ftxui::Mouse::Left, ftxui::Mouse::Moved, x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps, false, ctrl)) || handled;
    return send(mouse(ftxui::Mouse::Left, ftxui::Mouse::Released, x1, y1, false, ctrl)) || handled;
}

bool Harness::right_click(int x, int y) {
    const bool a = send(mouse(ftxui::Mouse::Right, ftxui::Mouse::Pressed, x, y, false, false));
    const bool b = send(mouse(ftxui::Mouse::Right, ftxui::Mouse::Released, x, y, false, false));
    return a || b;
}

std::optional<std::pair<int, int>> locate(const std::string& frame, std::string_view needle) {
    int y = 0;
    std::size_t start = 0;
    while (start <= frame.size()) {
        const auto end = frame.find('\n', start);
        const auto line = std::string_view{frame}.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (const auto at = line.find(needle); at != std::string_view::npos) {
            int x = 0;
            for (std::size_t i = 0; i < at; ++i)
                if ((static_cast<unsigned char>(line[i]) & 0xC0) != 0x80) ++x;
            return std::pair{x, y};
        }
        if (end == std::string::npos) break;
        start = end + 1;
        ++y;
    }
    return std::nullopt;
}

void check_snapshot(std::string_view name, const std::string& text) {
    const auto path = std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "snapshots" / (std::string{name} + ".txt");
    if (const char* update = std::getenv("SIGNALS_UPDATE_SNAPSHOTS"); update && std::string_view{update} == "1") {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << text;
        return;
    }
    std::ifstream in{path, std::ios::binary};
    INFO("snapshot " << path.string() << " — rerun with SIGNALS_UPDATE_SNAPSHOTS=1 to write it");
    REQUIRE(in.good());
    const std::string expected{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text == expected);
}

}  // namespace signals::test
