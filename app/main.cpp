#include <cstdio>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "core/thread_name.hpp"
#include "commands/cli.hpp"
#include "tui/run.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

bool interactive() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    signals::core::set_thread_name("signals-ui");
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    if (argc == 1 && interactive()) return signals::tui::run(std::nullopt, "dark");
    std::vector<std::string_view> args(argv, argv + argc);
    std::optional<signals::cli::TuiRequest> request;
    const int code = signals::cli::run_cli(args, std::cout, std::cerr, &request);
    if (request) return signals::tui::run(request->file, request->theme, request->language);
    return code;
}
