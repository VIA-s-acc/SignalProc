#include "commands/watch.hpp"

#include <fstream>
#include <iterator>
#include <string>

#include "pipeline/block.hpp"
#include "pipeline/sig.hpp"

namespace signals::cli {
namespace {

std::vector<std::filesystem::file_time_type> stamps(const std::vector<std::filesystem::path>& files) {
    std::vector<std::filesystem::file_time_type> out;
    for (const auto& f : files) {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(f, ec);
        out.push_back(ec ? std::filesystem::file_time_type::min() : t);
    }
    return out;
}

}  // namespace

std::vector<std::filesystem::path> watched_files(const std::filesystem::path& sig) {
    const auto absolute = std::filesystem::absolute(sig);
    std::vector<std::filesystem::path> out{absolute};
    std::ifstream in{sig, std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto doc = pipeline::parse_sig(text);
    if (!doc) return out;
    for (const auto& n : doc->nodes) {
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
