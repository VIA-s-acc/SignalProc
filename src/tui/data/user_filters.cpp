#include "tui/data/user_filters.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>

#include <fmt/format.h>
#include <toml++/toml.hpp>

namespace signals::tui {
namespace {

std::mutex g_mutex;
std::shared_ptr<const std::vector<UserBank>> g_banks = std::make_shared<const std::vector<UserBank>>();

std::string file_stem(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (c >= 0x80 || std::isalnum(c) || c == '-' || c == '_') out += static_cast<char>(c);
        else if (c == ' ') out += '_';
    }
    return out.empty() ? std::string{"bank"} : out;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

std::string number(double v) {
    auto s = fmt::format("{:.17g}", v);
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

std::string array(const std::vector<double>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + number(v[i]);
    return out + "]";
}

core::Result<core::Laurent> read_filter(const toml::table& t, std::string_view key) {
    const auto* arr = t[key].as_array();
    if (!arr || arr->empty()) return core::make_error(core::Errc::invalid_argument, std::string{key} + " is missing or empty");
    core::Laurent h;
    for (const auto& node : *arr) {
        const auto v = node.value<double>();
        if (!v) return core::make_error(core::Errc::invalid_argument, std::string{key} + " must contain numbers");
        h.coeffs.push_back(*v);
    }
    h.offset = static_cast<std::ptrdiff_t>(t[std::string{key} + "_offset"].value_or<std::int64_t>(0));
    return h;
}

}  // namespace

std::filesystem::path user_filters_dir(const EnvLookup& env) {
    if (auto xdg = env("XDG_CONFIG_HOME"); xdg && !xdg->empty()) return std::filesystem::path{*xdg} / "signals" / "filters";
    if (auto home = env("HOME"); home && !home->empty()) return std::filesystem::path{*home} / ".config" / "signals" / "filters";
    return std::filesystem::path{"signals-filters"};
}

core::Result<std::filesystem::path> save_user_bank(const std::filesystem::path& dir, const UserBank& bank) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return core::make_error(core::Errc::io_error, "cannot create " + dir.string() + ": " + ec.message());
    const auto stem = file_stem(bank.name);
    auto path = dir / (stem + ".flt");
    for (int n = 2; std::filesystem::exists(path); ++n) path = dir / (stem + "-" + std::to_string(n) + ".flt");
    std::ofstream out{path, std::ios::binary};
    if (!out) return core::make_error(core::Errc::io_error, "cannot write " + path.string());
    out << "name = " << quoted(bank.name) << "\n"
        << "kind = \"bank\"\n"
        << "h0_offset = " << bank.h0.offset << "\n"
        << "h0 = " << array(bank.h0.coeffs) << "\n"
        << "f0_offset = " << bank.f0.offset << "\n"
        << "f0 = " << array(bank.f0.coeffs) << "\n"
        << "note = " << quoted(bank.note) << "\n";
    if (!out) return core::make_error(core::Errc::io_error, "cannot write " + path.string());
    return path;
}

UserLibrary load_user_banks(const std::filesystem::path& dir) {
    UserLibrary lib;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return lib;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator{dir, ec})
        if (entry.path().extension() == ".flt") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        try {
            const auto t = toml::parse_file(f.string());
            auto h0 = read_filter(t, "h0");
            auto f0 = read_filter(t, "f0");
            if (!h0 || !f0) {
                lib.errors.push_back(f.filename().string() + ": " + (h0 ? f0.error().message : h0.error().message));
                continue;
            }
            lib.banks.push_back({t["name"].value_or(f.stem().string()), std::move(*h0), std::move(*f0), t["note"].value_or(std::string{})});
        } catch (const toml::parse_error& e) {
            lib.errors.push_back(f.filename().string() + ": " + std::string{e.description()});
        }
    }
    return lib;
}

void set_user_banks(std::vector<UserBank> banks) {
    auto next = std::make_shared<const std::vector<UserBank>>(std::move(banks));
    std::lock_guard lock{g_mutex};
    g_banks = std::move(next);
}

std::vector<UserBank> user_banks() {
    std::lock_guard lock{g_mutex};
    return *g_banks;
}

}  // namespace signals::tui
