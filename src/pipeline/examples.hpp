#pragma once

#include <span>
#include <string_view>

namespace signals::pipeline {

struct Example {
    std::string_view name, text;
};

std::span<const Example> examples();

}  // namespace signals::pipeline
