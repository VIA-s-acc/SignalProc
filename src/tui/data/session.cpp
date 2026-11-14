#include "tui/data/session.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

#include <toml++/toml.hpp>

#include "io/temp_file.hpp"

namespace signals::tui {
namespace {

std::string utf8(const std::filesystem::path& p) {
    const auto text = p.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path from_utf8(const std::string& text) { return std::filesystem::path{std::u8string{text.begin(), text.end()}}; }

std::vector<std::filesystem::path> paths(const toml::table& table, std::string_view key) {
    std::vector<std::filesystem::path> out;
    if (const auto* list = table[key].as_array())
        for (const auto& item : *list)
            if (const auto text = item.value<std::string>(); text && !text->empty() && out.size() < kRecentFiles) out.push_back(from_utf8(*text));
    return out;
}

toml::array array_of(const std::vector<std::filesystem::path>& list) {
    toml::array out;
    for (const auto& p : list) out.push_back(utf8(p));
    return out;
}

}  // namespace

std::filesystem::path session_path(const std::filesystem::path& prefs_file) { return prefs_file.parent_path() / "session.toml"; }

Session load_session(const std::filesystem::path& file) {
    Session s;
    std::ifstream in{file, std::ios::binary};
    if (!in) return s;
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error&) {
        return s;
    }
    if (const auto theme = table["theme"].value<std::string>(); theme && (*theme == "dark" || *theme == "light")) s.theme = *theme;
    if (const auto tab = table["tab"].value<std::int64_t>(); tab && *tab >= 0 && *tab < 100) s.tab = static_cast<int>(*tab);
    s.audio = paths(table, "audio");
    s.graphs = paths(table, "graphs");
    return s;
}

core::Result<void> save_session(const std::filesystem::path& file, const Session& session) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    toml::table table;
    if (!session.theme.empty()) table.insert("theme", session.theme);
    table.insert("tab", session.tab);
    table.insert("audio", array_of(session.audio));
    table.insert("graphs", array_of(session.graphs));
    std::ostringstream out;
    out << table << '\n';
    return io::write_file_atomic(file, out.str());
}

void remember(std::vector<std::filesystem::path>& list, const std::filesystem::path& file) {
    const auto clean = file.lexically_normal();
    std::erase(list, clean);
    list.insert(list.begin(), clean);
    if (list.size() > kRecentFiles) list.resize(kRecentFiles);
}

SessionStore::SessionStore(std::filesystem::path file) : file_{std::move(file)}, session_{load_session(file_)} {
    std::error_code ec;
    fresh_ = file_.empty() || !std::filesystem::exists(file_, ec);
}

void SessionStore::save_now() const {
    std::lock_guard lock{mutex_};
    save();
}

Session SessionStore::get() const {
    std::lock_guard lock{mutex_};
    return session_;
}

std::vector<std::filesystem::path> SessionStore::recent(RecentKind kind) const {
    std::vector<std::filesystem::path> out;
    {
        std::lock_guard lock{mutex_};
        out = kind == RecentKind::audio ? session_.audio : session_.graphs;
    }
    std::erase_if(out, [](const std::filesystem::path& p) {
        std::error_code ec;
        return !std::filesystem::is_regular_file(p, ec);
    });
    return out;
}

void SessionStore::remember(RecentKind kind, const std::filesystem::path& file) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(file, ec);
    if (ec) absolute = file;
    std::lock_guard lock{mutex_};
    tui::remember(kind == RecentKind::audio ? session_.audio : session_.graphs, absolute);
    save();
}

void SessionStore::forget_recent() {
    std::lock_guard lock{mutex_};
    session_.audio.clear();
    session_.graphs.clear();
    save();
}

void SessionStore::set_theme(std::string name) {
    std::lock_guard lock{mutex_};
    session_.theme = std::move(name);
    save();
}

void SessionStore::set_tab(int tab) {
    std::lock_guard lock{mutex_};
    session_.tab = tab;
    save();
}

void SessionStore::save() const {
    if (!file_.empty()) (void)save_session(file_, session_);
}

}  // namespace signals::tui
