#include "tui/term/external.hpp"

#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace signals::tui {

std::string base64(std::string_view bytes) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) | static_cast<unsigned char>(bytes[i + 2]);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (i < bytes.size()) {
        const bool two = i + 1 < bytes.size();
        const auto n = (static_cast<unsigned char>(bytes[i]) << 16) | (two ? static_cast<unsigned char>(bytes[i + 1]) << 8 : 0);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += two ? kAlphabet[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string osc52(std::string_view text) { return "\x1b]52;c;" + base64(text) + "\x07"; }

std::vector<std::string> editor_command(const std::filesystem::path& file, const EnvLookup& env, std::string_view configured) {
    for (const char* key : {"", "VISUAL", "EDITOR"}) {
        const auto value = *key ? env(key) : std::optional<std::string>{std::string{configured}};
        if (!value || value->empty()) continue;
        std::vector<std::string> argv;
        std::istringstream in{*value};
        for (std::string word; in >> word;) argv.push_back(word);
        if (argv.empty()) continue;
        argv.push_back(file.string());
        return argv;
    }
#if defined(_WIN32)
    return {"notepad", file.string()};
#elif defined(__APPLE__)
    return {"open", "-t", file.string()};
#else
    return {"xdg-open", file.string()};
#endif
}

bool launch(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
#ifdef _WIN32
    return _spawnvp(_P_WAIT, args[0], args.data()) != -1;
#else
    pid_t pid = 0;
    if (posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ) != 0) return false;
    int status = 0;
    return waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

}  // namespace signals::tui
