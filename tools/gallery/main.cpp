#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include "i18n/i18n.hpp"
#include "learn/answer.hpp"
#include "learn/checker.hpp"
#include "tui/app_screens.hpp"
#include "tui/data/learn_state.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/monitor/monitor.hpp"
#include "tui/render/svg_frame.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/shell/shell.hpp"
#include "tui/theme/theme.hpp"

namespace {

using namespace signals::tui;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

constexpr int kWidth = 150;
constexpr int kHeight = 44;
constexpr std::uintmax_t kStillLimit = 300 * 1024;
constexpr std::uintmax_t kAnimationLimit = 1536 * 1024;
constexpr std::uintmax_t kTotalLimit = 8 * 1024 * 1024;

class App {
public:
    explicit App(std::function<ResourceSnapshot()> resources) {
        ui_.theme = builtin_theme("dark");
        ui_.caps = {ColorDepth::truecolor, GlyphMode::unicode, "gallery"};
        ui_.terminal_size = [] { return std::pair{kWidth, kHeight}; };
        ui_.resources = std::move(resources);
        ui_.jobs = &jobs_;
        models_ = make_app_models();
        models_.signal->open(fs::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / "test.wav", jobs_);
        shell_ = make_shell(ui_, make_app_screens(ui_, models_), [] {});
        (void)frame();
    }

    ftxui::Screen frame() {
        settle();
        auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(kWidth), ftxui::Dimension::Fixed(kHeight));
        ftxui::Render(screen, shell_->Render());
        return screen;
    }

    void go(std::string_view route, const NavArgs& args = {}) {
        if (ui_.navigate) ui_.navigate(route, args);
    }
    void key(const ftxui::Event& e) { shell_->OnEvent(e); }
    void type(std::string_view text) {
        for (char c : text) key(ftxui::Event::Character(c));
    }
    AppModels& models() { return models_; }
    const SvgStyle style() const { return {.fg = ui_.theme.fg, .bg = ui_.theme.bg}; }

private:
    void settle() {
        for (int k = 0; k < 2000; ++k) {
            const bool busy = jobs_.queued() > 0 || models_.pipeline->run_view().running || models_.learn->view().checking;
            if (!busy) break;
            std::this_thread::sleep_for(5ms);
        }
    }

    JobRunner jobs_{[] {}, 0ms, true};
    UiContext ui_;
    AppModels models_;
    ftxui::Component shell_;
};

struct Step {
    std::function<void(App&)> act;
    double seconds = 1.5;
};

struct Scene {
    std::string name;
    std::vector<Step> steps;
    bool animated = false;
};

NavArgs args_for(std::string_view route) {
    if (route == "pipeline") return {.example = "first_steps"};
    if (route == "learn") return {.task = "0.1"};
    return {};
}

std::vector<Scene> scenes() {
    std::vector<Scene> out;
    for (const auto route : route_names()) {
        const std::string name{route};
        out.push_back({name, {{[name](App& app) { app.go(name, args_for(name)); }, 1.0}}, false});
    }

    Scene tour{"tour", {}, true};
    for (char k = '1'; k <= '9'; ++k) tour.steps.push_back({[k](App& app) { app.key(ftxui::Event::Character(k)); }, 1.6});
    out.push_back(std::move(tour));

    Scene run{"pipeline-run", {{[](App& app) { app.go("pipeline", args_for("pipeline")); }, 2.0}}, true};
    for (int k = 0; k < 3; ++k) run.steps.push_back({[](App& app) { app.key(ftxui::Event::ArrowRight); }, 1.2});
    run.steps.push_back({[](App& app) { app.key(ftxui::Event::Tab); }, 1.0});
    for (int k = 0; k < 2; ++k) run.steps.push_back({[](App& app) { app.key(ftxui::Event::ArrowRight); }, 2.0});
    out.push_back(std::move(run));

    Scene check{"learn-check", {{[](App& app) { app.go("learn", {.task = "1.1"}); }, 2.0}, {[](App& app) { app.key(ftxui::Event::Character('a')); }, 1.0}},
                true};
    check.steps.push_back({[](App& app) {
                               const auto* task = app.models().learn->active_task();
                               if (!task) return;
                               const auto values = signals::learn::expected_answer(*task, app.models().learn->variant());
                               if (values) app.type(signals::learn::format_answer(*values, signals::learn::variant_answer(*task, app.models().learn->variant())));
                           },
                           1.5});
    check.steps.push_back({[](App& app) { app.key(ftxui::Event::Return); }, 3.0});
    out.push_back(std::move(check));

    Scene pick{"designer-pick", {{[](App& app) { app.go("designer"); }, 1.6}}, true};
    for (int k = 0; k < 4; ++k) pick.steps.push_back({[](App& app) { app.key(ftxui::Event::Character('n')); }, 1.6});
    out.push_back(std::move(pick));
    return out;
}

int run(const fs::path& out_dir, std::optional<std::string> only) {
    ResourceMonitor monitor{[] {}, 100ms};
    std::this_thread::sleep_for(300ms);
    const auto resources = [&monitor] { return monitor.latest(); };
    fs::create_directories(out_dir);
    std::uintmax_t total = 0;
    int failures = 0;
    for (const auto& scene : scenes()) {
        if (only && *only != scene.name) continue;
        App app{resources};
        std::vector<SvgFrame> frames;
        for (const auto& step : scene.steps) {
            step.act(app);
            frames.push_back({app.frame(), step.seconds});
        }
        const auto svg = scene.animated ? svg_animation(frames, app.style()) : svg_still(frames.back().screen, app.style());
        const auto file = out_dir / (scene.name + ".svg");
        std::ofstream{file, std::ios::binary} << svg;
        const auto size = fs::file_size(file);
        total += size;
        const auto limit = scene.animated ? kAnimationLimit : kStillLimit;
        std::printf("%-16s %8.1f KB%s\n", scene.name.c_str(), static_cast<double>(size) / 1024, size > limit ? "  too large" : "");
        if (size > limit) ++failures;
    }
    std::printf("total %.1f KB\n", static_cast<double>(total) / 1024);
    if (total > kTotalLimit) {
        std::printf("total is over %ju KB\n", kTotalLimit / 1024);
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path out = "docs/images/en";
    std::optional<std::string> only;
    signals::Language language = signals::Language::en;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--out" && i + 1 < argc) out = argv[++i];
        else if (arg == "--only" && i + 1 < argc) only = argv[++i];
        else if (arg == "--lang" && i + 1 < argc) {
            const auto parsed = signals::parse_language(argv[++i]);
            if (!parsed) {
                std::fprintf(stderr, "unknown language: %s\n", argv[i]);
                return 2;
            }
            language = *parsed;
        } else {
            std::fprintf(stderr, "usage: signals-gallery [--out DIR] [--lang en|ru] [--only NAME]\n");
            return 2;
        }
    }
    signals::set_language(language);
    ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);
    return run(out, only);
}
