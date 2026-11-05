#include <catch2/catch_test_macros.hpp>

#include <ftxui/dom/elements.hpp>

#include "tui/harness.hpp"

TEST_CASE("render_text keeps characters and trims rows", "[tui]") {
    using namespace ftxui;
    CHECK(signals::test::render_text(vbox({text("ab"), text("c")}), 5, 3) == "ab\nc\n");
}
