#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include "support/paths.hpp"
#include "tui/data/session.hpp"

using namespace signals::tui;
namespace fs = std::filesystem;

namespace {

fs::path fresh_dir(const std::string& name) {
    const auto dir = signals::test::temp_path(name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

fs::path touch(const fs::path& file) {
    std::ofstream{file} << "x";
    return file;
}

}  // namespace

TEST_CASE("the session file lives next to terminal.toml", "[tui][session]") {
    CHECK(session_path(fs::path{"/home/u/.config/signals/terminal.toml"}) == fs::path{"/home/u/.config/signals/session.toml"});
}

TEST_CASE("remember puts a file first, drops its old place and keeps ten", "[tui][session]") {
    std::vector<fs::path> list;
    for (int k = 0; k < 12; ++k) remember(list, fs::path{"/a/f" + std::to_string(k) + ".wav"});
    REQUIRE(list.size() == kRecentFiles);
    CHECK(list.front() == fs::path{"/a/f11.wav"});
    CHECK(list.back() == fs::path{"/a/f2.wav"});
    remember(list, fs::path{"/a/./f5.wav"});
    CHECK(list.front() == fs::path{"/a/f5.wav"});
    CHECK(std::count(list.begin(), list.end(), fs::path{"/a/f5.wav"}) == 1);
    CHECK(list.size() == kRecentFiles);
}

TEST_CASE("a session is saved and read back, Cyrillic paths included", "[tui][session]") {
    const auto dir = fresh_dir("session-roundtrip");
    Session s;
    s.theme = "light";
    s.tab = 7;
    s.audio = {dir / u8"запись.flac", dir / "b.wav"};
    s.graphs = {dir / "g.sig"};
    REQUIRE(save_session(dir / "session.toml", s));
    const auto back = load_session(dir / "session.toml");
    CHECK(back.theme == "light");
    CHECK(back.tab == 7);
    CHECK(back.audio == s.audio);
    CHECK(back.graphs == s.graphs);
}

TEST_CASE("a missing or broken session file gives an empty session", "[tui][session]") {
    const auto dir = fresh_dir("session-broken");
    CHECK(load_session(dir / "none.toml").audio.empty());
    std::ofstream{dir / "bad.toml"} << "theme = [unclosed\n";
    const auto bad = load_session(dir / "bad.toml");
    CHECK(bad.theme.empty());
    CHECK(bad.tab == 0);
    std::ofstream{dir / "odd.toml"} << "tab = -3\ntheme = \"neon\"\naudio = [1, \"/x.wav\"]\n";
    const auto odd = load_session(dir / "odd.toml");
    CHECK(odd.tab == 0);
    CHECK(odd.theme.empty());
    CHECK(odd.audio == std::vector<fs::path>{fs::path{"/x.wav"}});
}

TEST_CASE("the store saves every change and lists only files that still exist", "[tui][session]") {
    const auto dir = fresh_dir("session-store");
    const auto file = dir / "session.toml";
    const auto a = touch(dir / "a.wav"), b = touch(dir / "b.mp3"), g = touch(dir / "g.sig");
    {
        SessionStore store{file};
        store.remember(RecentKind::audio, a);
        store.remember(RecentKind::audio, b);
        store.remember(RecentKind::graph, g);
        store.set_theme("light");
        store.set_tab(3);
    }
    fs::remove(a);
    SessionStore again{file};
    CHECK(again.get().theme == "light");
    CHECK(again.get().tab == 3);
    CHECK(again.get().audio.size() == 2);
    CHECK(again.recent(RecentKind::audio) == std::vector<fs::path>{b});
    CHECK(again.recent(RecentKind::graph) == std::vector<fs::path>{g});
    again.forget_recent();
    CHECK(SessionStore{file}.get().audio.empty());
    CHECK(SessionStore{file}.get().graphs.empty());
    CHECK(SessionStore{file}.get().theme == "light");
}

TEST_CASE("a session store is fresh only when its file was absent", "[tui][session][start]") {
    const auto dir = fresh_dir("session-fresh");
    const auto file = dir / "session.toml";
    {
        SessionStore store{file};
        CHECK(store.fresh());
        store.save_now();
    }
    CHECK(fs::exists(file));
    CHECK_FALSE(SessionStore{file}.fresh());
    std::ofstream{dir / "broken.toml"} << "this is [ not toml";
    CHECK_FALSE(SessionStore{dir / "broken.toml"}.fresh());
    CHECK(SessionStore{fs::path{}}.fresh());
}
