#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/event.hpp>

#include "audio/manual_sink.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/examples.hpp"
#include "support/paths.hpp"
#include "tui/app_screens.hpp"
#include "tui/harness.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/shell/shell.hpp"
#include "tui/ui_fixture.hpp"
#include "tui/widgets/text_util.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;
using signals::test::Harness;

namespace {

bool right_edge(const std::string& c) { return c == "│" || c == "╮" || c == "╯" || c == "┤" || c == "┃" || c == "║" || c == "┐" || c == "┘"; }

bool word(const std::string& c) {
    if (c.empty()) return false;
    if (c.size() == 1) return std::isalnum(static_cast<unsigned char>(c[0])) != 0;
    const auto lead = static_cast<unsigned char>(c[0]);
    return c.size() == 2 && (lead == 0xD0 || lead == 0xD1 || lead == 0xCE || lead == 0xCF);
}

std::string join(const std::vector<std::string>& cps, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t k = from; k < to; ++k) out += cps[k];
    return out;
}

std::vector<std::string> lines_of(const std::string& frame) {
    std::vector<std::string> out;
    std::istringstream in{frame};
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

struct Suspect {
    std::string tail;
    std::size_t line, column;
};

std::vector<Suspect> suspects(const std::vector<std::string>& lines, int width) {
    std::vector<Suspect> out;
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const auto cps = split_codepoints(lines[row]);
        auto add = [&](std::size_t end) {
            std::size_t from = end;
            while (from > 0 && end - from < 24 && !right_edge(cps[from - 1])) --from;
            out.push_back({join(cps, from, end), row, end});
        };
        for (std::size_t i = 3; i < cps.size(); ++i) {
            if (!right_edge(cps[i])) continue;
            const auto end = cps[i - 1] == " " ? i - 1 : i;
            if (word(cps[end - 1]) && word(cps[end - 2])) add(end);
        }
        if (static_cast<int>(cps.size()) >= width && cps.size() >= 2 && word(cps.back()) && word(cps[cps.size() - 2])) add(cps.size());
    }
    return out;
}

std::string continuation(const std::string& wider, const std::string& tail) {
    for (std::size_t at = wider.find(tail); at != std::string::npos; at = wider.find(tail, at + 1)) {
        const auto rest = split_codepoints(wider.substr(at + tail.size(), 12));
        std::string next;
        for (std::size_t k = 0; k < rest.size() && k < 3 && word(rest[k]); ++k) next += rest[k];
        if (!next.empty()) return next;
    }
    return {};
}

bool wrapped(const std::vector<std::string>& lines, std::size_t row, std::size_t column, const std::string& next) {
    if (row + 1 >= lines.size()) return false;
    const auto cps = split_codepoints(lines[row + 1]);
    std::size_t end = std::min(column, cps.size()), from = end;
    while (from > 0 && !right_edge(cps[from - 1])) --from;
    while (from < end && cps[from] == " ") ++from;
    return join(cps, from, end).starts_with(next);
}

struct World {
    JobRunner jobs{[] {}, 0ms, true};
    signals::test::ManualSink* raw = nullptr;
    signals::audio::Player player = signals::test::manual_player(raw);
    std::shared_ptr<UiContext> ui;
    AppModels models = make_app_models();
    std::vector<std::shared_ptr<TabScreen>> screens;
    Harness shell;

    World(int w, int h) : ui{signals::test::make_ui(w, h)} {
        ui->jobs = &jobs;
        ui->player = &player;
        models.signal->open(signals::test::sample("test.wav"), jobs);
        screens = make_app_screens(*ui, models);
        shell = Harness{make_shell(*ui, screens, [] {}), w, h};
    }
};

void each_state(World& world, const std::function<void(const std::string& where, const std::string& behind)>& look) {
    auto& shell = world.shell;
    const std::pair<char, int> tabs[]{{'1', 1}, {'2', 3}, {'3', 1}, {'4', 2}, {'5', 1}, {'6', 1}, {'7', 1}, {'8', 1}, {'9', 3}};
    for (const auto& [key, subtabs] : tabs) {
        shell.send(ftxui::Event::Character(key));
        const auto tab = static_cast<std::size_t>(key - '1');
        for (int s = 0; s < subtabs; ++s) {
            world.screens[tab]->select_child(static_cast<std::size_t>(s));
            shell.frame();
            const auto where = "tab " + std::string{key} + "." + std::to_string(s);
            look(where, {});
            for (int panel = 0; panel < 5; ++panel) {
                shell.send(ftxui::Event::Character('z'));
                look(where + " z" + std::to_string(panel), {});
                shell.send(ftxui::Event::Escape);
                shell.send(ftxui::Event::Tab);
            }
            const auto behind = shell.frame();
            shell.send(ftxui::Event::Character('?'));
            look(where + " help", behind);
            shell.send(ftxui::Event::Escape);
            shell.send(ftxui::Event::Character(':'));
            look(where + " palette", behind);
            shell.send(ftxui::Event::Escape);
        }
    }
    shell.send(ftxui::Event::Character('8'));
    for (const auto& ex : signals::pipeline::examples()) {
        REQUIRE(world.models.pipeline->load_text(ex.name, ex.text));
        world.models.pipeline->run(world.jobs);
        for (int k = 0; k < 4000 && world.models.pipeline->run_view().running; ++k) std::this_thread::sleep_for(5ms);
        std::vector<std::string> ids;
        for (const auto& n : world.models.pipeline->doc().nodes) ids.push_back(n.id);
        for (const auto& id : ids) {
            world.models.pipeline->selection = {Selection::Kind::node, id, {}};
            look("pipeline " + std::string{ex.name} + " node " + id, {});
        }
        shell.send(ftxui::Event::Tab);
        for (int p = 0; p < 6; ++p) {
            shell.send(ftxui::Event::ArrowRight);
            look("pipeline " + std::string{ex.name} + " probe " + std::to_string(p), {});
        }
        shell.send(ftxui::Event::TabReverse);
    }
}

}  // namespace

TEST_CASE("no text is clipped silently on any screen, size or language", "[tui][fit][audit]") {
    std::map<std::string, std::set<std::string>> findings;
    for (const auto language : {signals::Language::ru, signals::Language::en}) {
        const signals::LanguageScope scope{language};
        for (auto [w, h] : {std::pair{80, 24}, std::pair{110, 32}, std::pair{150, 44}}) {
            World narrow{w, h}, wide{w + 40, h};
            struct Seen {
                std::string where, frame, behind;
            };
            std::vector<Seen> frames;
            each_state(narrow, [&](const std::string& where, const std::string& behind) { frames.push_back({where, narrow.shell.frame(), behind}); });
            std::size_t k = 0;
            each_state(wide, [&](const std::string& where, const std::string&) {
                REQUIRE(k < frames.size());
                REQUIRE(frames[k].where == where);
                const auto wider = wide.shell.frame();
                const auto lines = lines_of(frames[k].frame);
                for (const auto& [tail, row, column] : suspects(lines, w)) {
                    const auto next = continuation(wider, tail);
                    if (next.empty() || wrapped(lines, row, column, next) || frames[k].behind.find(tail) != std::string::npos) continue;
                    findings[where + " · …" + tail].insert(std::string{language == signals::Language::en ? "en " : "ru "} + std::to_string(w) + "x" + std::to_string(h));
                }
                ++k;
            });
        }
    }
    for (const auto& [what, where] : findings) {
        std::string sizes;
        for (const auto& w : where) sizes += (sizes.empty() ? "" : ", ") + w;
        UNSCOPED_INFO(what << "   [" << sizes << "]");
    }
    CHECK(findings.empty());
}

