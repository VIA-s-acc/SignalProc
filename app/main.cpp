#include <iostream>
#include <string_view>
#include <vector>

#include "commands/cli.hpp"

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv, argv + argc);
    return signals::cli::run_cli(args, std::cout, std::cerr);
}
