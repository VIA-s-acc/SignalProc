#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "core/error.hpp"

namespace signals::tui {

enum class RecentKind { audio, graph };

struct Session {
    std::string theme;
    int tab = 0;
    std::vector<std::filesystem::path> audio, graphs;
};

inline constexpr std::size_t kRecentFiles = 10;

std::filesystem::path session_path(const std::filesystem::path& prefs_file);
Session load_session(const std::filesystem::path& file);
core::Result<void> save_session(const std::filesystem::path& file, const Session& session);
void remember(std::vector<std::filesystem::path>& list, const std::filesystem::path& file);

class SessionStore {
public:
    explicit SessionStore(std::filesystem::path file);
    Session get() const;
    std::vector<std::filesystem::path> recent(RecentKind kind) const;
    void remember(RecentKind kind, const std::filesystem::path& file);
    void forget_recent();
    void set_theme(std::string name);
    void set_tab(int tab);
    bool fresh() const { return fresh_; }
    void save_now() const;

private:
    void save() const;
    std::filesystem::path file_;
    mutable std::mutex mutex_;
    Session session_;
    bool fresh_ = true;
};

}  // namespace signals::tui
