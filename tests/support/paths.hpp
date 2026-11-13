#pragma once

#include <filesystem>
#include <string>

namespace signals::test {

inline std::filesystem::path fixture(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "wav" / name;
}

inline std::filesystem::path audio_fixture(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "audio" / name;
}

inline std::filesystem::path learn_fixture(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "learn" / name;
}

inline std::filesystem::path sample(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "samples" / name;
}

inline std::string sample_str(const std::string& name) { return sample(name).string(); }

inline std::filesystem::path temp_path(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / "signals-tests";
    std::filesystem::create_directories(dir);
    return dir / name;
}

}  // namespace signals::test
