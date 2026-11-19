#include "pipeline/probe_files.hpp"

#include <set>
#include <string_view>

namespace signals::pipeline {

namespace {

std::string safe_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        const bool bad = u < 0x20 || u == 0x7f || std::string_view{"/\\:*?\"<>|"}.find(c) != std::string_view::npos;
        out += bad ? '_' : c;
    }
    return out.empty() ? std::string{"_"} : out;
}

// macOS and Windows file systems fold ASCII case, so "Y" and "y" would overwrite each other.
std::string fold(std::string_view name) {
    std::string out{name};
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

}  // namespace

std::map<std::string, std::string, std::less<>> probe_file_names(std::span<const std::string> probes) {
    std::map<std::string, std::string, std::less<>> out;
    std::set<std::string, std::less<>> used;
    for (const auto& probe : probes) {
        if (out.contains(probe)) continue;
        const auto base = safe_name(probe);
        auto name = base;
        for (int k = 2; used.contains(fold(name)); ++k) name = base + "-" + std::to_string(k);
        used.insert(fold(name));
        out.emplace(probe, std::move(name));
    }
    return out;
}

}  // namespace signals::pipeline
