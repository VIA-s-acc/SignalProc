#pragma once

#include <string>

#include "core/error.hpp"

namespace signals::tui {

std::string error_text(const core::Error& error);

}  // namespace signals::tui
