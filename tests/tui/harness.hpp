#pragma once

#include <string>
#include <string_view>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace signals::test {

std::string render_text(ftxui::Element element, int width, int height);

struct Harness {
    ftxui::Component root;
    int w = 150;
    int h = 44;

    std::string frame();
    bool send(ftxui::Event event);
    bool click(int x, int y, bool shift = false);
    bool wheel(int x, int y, bool up, bool shift = false);
    bool drag(int x0, int y0, int x1, int y1, bool ctrl = false);
};

void check_snapshot(std::string_view name, const std::string& text);

}  // namespace signals::test
