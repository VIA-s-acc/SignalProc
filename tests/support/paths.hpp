#pragma once

#include <filesystem>
#include <string>

namespace signals::test {

inline std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "wav" / name;
}

inline std::filesystem::path sample(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / name;
}

inline std::string sample_str(const std::string& name) { return sample(name).string(); }

}  // namespace signals::test
