#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/laurent.hpp"
#include "tui/term/caps.hpp"

namespace signals::tui {

struct UserBank {
    std::string name;
    core::Laurent h0, f0;
    std::string note;
};

struct UserFilter {
    std::string name;
    core::Laurent h;
    std::string note;
};

struct CustomFilter {
    std::string name;
    core::Laurent h;
};

inline constexpr std::size_t kMaxCustomTaps = 4096;
core::Result<CustomFilter> parse_custom_filter(std::string_view text);

struct UserLibrary {
    std::vector<UserBank> banks;
    std::vector<UserFilter> filters;
    std::vector<std::string> errors;
};

std::filesystem::path user_filters_dir(const EnvLookup& env);
core::Result<std::filesystem::path> save_user_bank(const std::filesystem::path& dir, UserBank& bank);
core::Result<std::filesystem::path> save_user_filter(const std::filesystem::path& dir, UserFilter& filter);
UserLibrary load_user_banks(const std::filesystem::path& dir);

void set_user_banks(std::vector<UserBank> banks);
std::vector<UserBank> user_banks();
void set_user_filters(std::vector<UserFilter> filters);
std::vector<UserFilter> user_filters();

}  // namespace signals::tui
