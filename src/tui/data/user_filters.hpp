#pragma once

#include <filesystem>
#include <string>
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

struct UserLibrary {
    std::vector<UserBank> banks;
    std::vector<std::string> errors;
};

std::filesystem::path user_filters_dir(const EnvLookup& env);
core::Result<std::filesystem::path> save_user_bank(const std::filesystem::path& dir, UserBank& bank);
UserLibrary load_user_banks(const std::filesystem::path& dir);

void set_user_banks(std::vector<UserBank> banks);
std::vector<UserBank> user_banks();

}  // namespace signals::tui
