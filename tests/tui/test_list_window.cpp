#include <catch2/catch_test_macros.hpp>

#include "tui/widgets/list_window.hpp"

using signals::tui::list_window;

TEST_CASE("a list that fits shows every row from the top", "[tui][list_window]") {
    std::size_t first = 3;
    const auto w = list_window(first, 5, 10, 1, std::nullopt);
    CHECK(w.first == 0);
    CHECK(w.shown == 5);
}

TEST_CASE("a long list keeps a row for each marker", "[tui][list_window]") {
    std::size_t first = 0;
    auto w = list_window(first, 20, 6, 1, std::nullopt);
    CHECK(w.first == 0);
    CHECK(w.shown == 5);
    first = 4;
    w = list_window(first, 20, 6, 1, std::nullopt);
    CHECK(w.first == 4);
    CHECK(w.shown == 4);
}

TEST_CASE("scrolling past the end clamps to the last full window", "[tui][list_window]") {
    std::size_t first = 100;
    const auto w = list_window(first, 20, 6, 1, std::nullopt);
    CHECK(w.first + w.shown == 20);
    CHECK(first == w.first);
}

TEST_CASE("the window follows a row only when asked", "[tui][list_window]") {
    std::size_t first = 0;
    auto w = list_window(first, 20, 6, 1, 12);
    CHECK(w.first <= 12);
    CHECK(12 < w.first + w.shown);
    first = 0;
    w = list_window(first, 20, 6, 1, std::nullopt);
    CHECK(w.first == 0);
}

TEST_CASE("two-line rows count twice against the budget", "[tui][list_window]") {
    std::size_t first = 0;
    const auto w = list_window(first, 18, 11, 2, std::nullopt);
    CHECK(w.shown == 5);
}
