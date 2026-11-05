#pragma once

#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "commands/cli.hpp"

namespace signals::test {

inline std::tuple<int, std::string, std::string> run_cli(std::initializer_list<std::string_view> args) {
    std::ostringstream out, err;
    const std::vector<std::string_view> v(args);
    const int code = signals::cli::run_cli(v, out, err);
    return {code, out.str(), err.str()};
}

}  // namespace signals::test
