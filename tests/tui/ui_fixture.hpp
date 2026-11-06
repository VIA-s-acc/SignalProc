#pragma once

#include <memory>
#include <utility>

#include "tui/shell/screen.hpp"

namespace signals::test {

inline tui::ResourceSnapshot fixed_resources() {
    tui::ResourceSnapshot s;
    s.cpu_one_core_pct = 3.1;
    s.cpu_all_cores_pct = 3.1 / 8;
    s.rss = 182u << 20;
    s.peak_rss = 240u << 20;
    s.threads = 4;
    s.fps = 60;
    s.core_load_pct.assign(8, 10.0);
    s.thread_load = {{"signals-ui", 2.0, 5.0}, {"signals-jobs", 1.1, 2.0}, {"", 0.0, 0.1}};
    s.cpu_seconds = 7.1;
    s.ram_share_pct = 0.74;
    s.build = "release";
    return s;
}

inline std::shared_ptr<tui::UiContext> make_ui(int w, int h, tui::GlyphMode glyphs = tui::GlyphMode::unicode,
                                               tui::ColorDepth depth = tui::ColorDepth::truecolor) {
    auto ui = std::make_shared<tui::UiContext>();
    ui->theme = tui::builtin_theme("dark");
    ui->caps = {depth, glyphs, "test"};
    ui->resources = [] { return fixed_resources(); };
    ui->terminal_size = [w, h] { return std::pair{w, h}; };
    return ui;
}

}  // namespace signals::test
