#include "tui/run.hpp"

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "i18n/i18n.hpp"

namespace signals::tui {

int run(std::optional<std::filesystem::path>, std::string_view) {
    auto app = ftxui::App::Fullscreen();
    auto root = ftxui::Renderer([] { return ftxui::text(tr("tui.title")) | ftxui::bold; });
    root |= ftxui::CatchEvent([&app](const ftxui::Event& e) {
        if (e == ftxui::Event::Character('q')) {
            app.Exit();
            return true;
        }
        return false;
    });
    app.Loop(root);
    return 0;
}

}  // namespace signals::tui
