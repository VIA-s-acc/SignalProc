#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "commands/cli.hpp"
#include "tui/run.hpp"

int main(int argc, char** argv) {
    if (argc == 1 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) return signals::tui::run(std::nullopt, "dark");
    std::vector<std::string_view> args(argv, argv + argc);
    std::optional<signals::cli::TuiRequest> request;
    const int code = signals::cli::run_cli(args, std::cout, std::cerr, &request);
    if (request) return signals::tui::run(request->file, request->theme);
    return code;
}
