#pragma once

#include <map>
#include <span>
#include <string>

namespace signals::pipeline {

std::map<std::string, std::string, std::less<>> probe_file_names(std::span<const std::string> probes);

}  // namespace signals::pipeline
