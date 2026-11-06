#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>

#include "tui/term/external.hpp"

using namespace signals::tui;

TEST_CASE("osc 52 carries the text in base64", "[tui][external]") {
    CHECK(base64("") == "");
    CHECK(base64("f") == "Zg==");
    CHECK(base64("fo") == "Zm8=");
    CHECK(base64("foo") == "Zm9v");
    CHECK(base64("a_j[k]") == "YV9qW2td");
    CHECK(osc52("hi") == "\x1b]52;c;aGk=\x07");
}

TEST_CASE("the editor comes from VISUAL, then EDITOR, then the platform opener", "[tui][external]") {
    std::map<std::string, std::string> vars{{"EDITOR", "nano"}, {"VISUAL", "code -w"}};
    auto env = [&](std::string_view k) -> std::optional<std::string> {
        auto it = vars.find(std::string{k});
        return it == vars.end() ? std::nullopt : std::optional{it->second};
    };
    CHECK(editor_command("/x/a.cpp", env) == std::vector<std::string>{"code", "-w", "/x/a.cpp"});
    vars.erase("VISUAL");
    CHECK(editor_command("/x/a.cpp", env) == std::vector<std::string>{"nano", "/x/a.cpp"});
    vars.clear();
    const auto fallback = editor_command("/x/a.cpp", env);
    REQUIRE_FALSE(fallback.empty());
    CHECK(fallback.back() == "/x/a.cpp");
}
