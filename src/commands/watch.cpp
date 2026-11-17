#include "commands/watch.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <chrono>
#include <thread>
#include <fstream>
#include <iterator>
#include <string>

#include "pipeline/block.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/sig.hpp"

namespace signals::cli {
namespace {

std::atomic<bool> g_stopped{false};

struct Stamp {
    std::filesystem::file_time_type time;
    std::uintmax_t size = 0;
    std::size_t content = 0;
    bool operator==(const Stamp&) const = default;
};

constexpr std::uintmax_t kHashedBytes = 256 * 1024;

// File times can be one second coarse (libstdc++ on macOS, HFS+, FAT, network shares): two saves within a second keep the
// same time, so small files are compared by content as well.
std::vector<Stamp> stamps(const std::vector<std::filesystem::path>& files) {
    std::vector<Stamp> out;
    for (const auto& f : files) {
        Stamp s;
        std::error_code ec;
        s.time = std::filesystem::last_write_time(f, ec);
        if (ec) s.time = std::filesystem::file_time_type::min();
        s.size = std::filesystem::file_size(f, ec);
        if (ec) s.size = 0;
        else if (s.size <= kHashedBytes) {
            std::ifstream in{f, std::ios::binary};
            s.content = std::hash<std::string>{}(std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}});
        }
        out.push_back(s);
    }
    return out;
}

}  // namespace

void stop_watching() { g_stopped.store(true); }
bool watching_stopped() { return g_stopped.load(); }
void restart_watching() { g_stopped.store(false); }

std::vector<std::filesystem::path> watched_files(const std::filesystem::path& sig) {
    const auto absolute = std::filesystem::absolute(sig);
    std::vector<std::filesystem::path> out{absolute};
    std::ifstream in{sig, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto loaded = pipeline::load_sig(text);
    if (!loaded) return out;
    for (const auto& n : loaded->doc.nodes) {
        const auto* type = pipeline::find_block_type(n.type);
        if (!type || type->side_effect) continue;
        for (const auto& spec : type->params) {
            if (spec.kind != pipeline::ParamSpec::Kind::path) continue;
            const auto it = n.params.find(spec.name);
            if (it == n.params.end() || it->second.empty()) continue;
            auto path = pipeline::utf8_path(it->second);
            out.push_back(path.is_relative() ? (absolute.parent_path() / path).lexically_normal() : path);
        }
    }
    return out;
}

std::optional<std::string> read_settled(const std::filesystem::path& file) {
    const auto read = [&]() -> std::optional<std::string> {
        std::ifstream in{file, std::ios::binary};
        if (!in) return std::nullopt;
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    };
    auto text = read();
    for (int k = 0; k < 10; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds{30});
        auto again = read();
        if (again == text) break;
        text = std::move(again);
    }
    return text;
}

int watch_loop(const std::function<int()>& once, const std::function<std::vector<std::filesystem::path>()>& files, const std::function<bool()>& keep_going,
               const std::function<void()>& pause) {
    int code = once();
    auto last = stamps(files());
    while (keep_going()) {
        pause();
        if (stamps(files()) == last) continue;
        // Editors often save in several writes; rereading at once can run a half-written file.
        pause();
        last = stamps(files());
        code = once();
    }
    return code;
}

}  // namespace signals::cli
