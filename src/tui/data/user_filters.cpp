#include "tui/data/user_filters.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <locale>
#include <sstream>
#include <set>
#include <memory>
#include <mutex>
#include <system_error>

#include <fmt/format.h>
#include <toml++/toml.hpp>

#include "i18n/i18n.hpp"

namespace signals::tui {
namespace {

std::mutex g_mutex;
std::shared_ptr<const std::vector<UserBank>> g_banks = std::make_shared<const std::vector<UserBank>>();
std::shared_ptr<const std::vector<UserFilter>> g_filters = std::make_shared<const std::vector<UserFilter>>();

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool real_number(std::string_view s, double& out) {
    if (s.empty()) return false;
    std::istringstream in{std::string{s}};
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.peek() == std::char_traits<char>::eof() && std::isfinite(out);
}

void push_number_token(std::vector<std::string>& out, std::string token) {
    if (token.empty()) return;
    const auto comma = token.find(',');
    if (comma == std::string::npos) {
        out.push_back(std::move(token));
        return;
    }
    const bool single = token.find(',', comma + 1) == std::string::npos && token.find('.') == std::string::npos;
    if (single && comma > 0 && comma + 1 < token.size() && std::isdigit(static_cast<unsigned char>(token[comma - 1])) &&
        std::isdigit(static_cast<unsigned char>(token[comma + 1]))) {
        token[comma] = '.';
        out.push_back(std::move(token));
        return;
    }
    std::string piece;
    for (const char c : token) {
        if (c == ',') {
            if (!piece.empty()) out.push_back(std::exchange(piece, {}));
        } else {
            piece += c;
        }
    }
    if (!piece.empty()) out.push_back(piece);
}

std::vector<std::string> tokens(std::string_view s) {
    std::string text;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s.substr(i).starts_with("−")) {
            text += '-';
            i += std::string_view{"−"}.size() - 1;
        } else {
            text += s[i];
        }
    }
    std::vector<std::string> out;
    std::string cur;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ';') push_number_token(out, std::exchange(cur, {}));
        else cur += c;
    }
    push_number_token(out, cur);
    return out;
}

bool root_token(std::string_view s, std::vector<std::complex<double>>& roots) {
    double re = 0;
    if (real_number(s, re)) {
        roots.emplace_back(re, 0.0);
        return true;
    }
    if (s.size() < 2 || (s.back() != 'j' && s.back() != 'i')) return false;
    s.remove_suffix(1);
    std::size_t split = s.find("±"), width = 2;
    bool minus = false;
    if (split == std::string_view::npos) {
        width = 1;
        for (std::size_t i = 1; i < s.size(); ++i)
            if ((s[i] == '+' || s[i] == '-') && s[i - 1] != 'e' && s[i - 1] != 'E') split = i, minus = s[i] == '-';
    }
    double im = 0;
    if (split == std::string_view::npos) {
        if (!real_number(s, im)) return false;
        re = 0;
    } else if (!real_number(s.substr(0, split), re) || !real_number(s.substr(split + width), im)) {
        return false;
    }
    if (minus) im = -im;
    roots.emplace_back(re, im);
    if (im != 0) roots.emplace_back(re, -im);
    return true;
}

core::Error custom_error(const std::string& message) { return core::Error{core::Errc::invalid_argument, message}; }

std::string file_stem(const std::string& name) {
    constexpr std::size_t kMaxStem = 120;
    std::string out;
    for (unsigned char c : name) {
        if (c >= 0x80 || std::isalnum(c) || c == '-' || c == '_') out += static_cast<char>(c);
        else if (c == ' ') out += '_';
    }
    if (out.size() > kMaxStem) {
        std::size_t cut = kMaxStem;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out.empty() ? std::string{"bank"} : out;
}

std::string toml_string(const std::string& s) {
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

core::Result<CustomFilter> parse_custom_filter(std::string_view text) {
    CustomFilter out;
    text = trim(text);
    if (const auto eq = text.find('='); eq != std::string_view::npos) {
        out.name = std::string{trim(text.substr(0, eq))};
        if (out.name.empty()) return std::unexpected{custom_error(tr("tui.filters.custom.error.name"))};
        text = trim(text.substr(eq + 1));
    }
    if (text.empty()) return std::unexpected{custom_error(tr("tui.filters.custom.error.empty"))};
    const bool roots = text.starts_with("z:") || text.starts_with("Z:");
    const auto parts = tokens(roots ? text.substr(2) : text);
    if (parts.size() + (roots ? 1 : 0) > kMaxCustomTaps)
        return std::unexpected{custom_error(fmt::format(fmt::runtime(tr("tui.filters.custom.error.too_long")), fmt::arg("max", kMaxCustomTaps)))};
    if (!roots) {
        for (const auto& t : parts) {
            double v = 0;
            if (!real_number(t, v)) return std::unexpected{custom_error(fmt::format(fmt::runtime(tr("tui.filters.custom.error.number")), fmt::arg("token", t)))};
            out.h.coeffs.push_back(v);
        }
        if (out.h.coeffs.empty()) return std::unexpected{custom_error(tr("tui.filters.custom.error.empty"))};
        return out;
    }
    std::vector<std::complex<double>> zeros;
    for (const auto& t : parts)
        if (!root_token(t, zeros)) return std::unexpected{custom_error(fmt::format(fmt::runtime(tr("tui.filters.custom.error.number")), fmt::arg("token", t)))};
    if (zeros.empty()) return std::unexpected{custom_error(tr("tui.filters.custom.error.roots"))};
    if (zeros.size() + 1 > kMaxCustomTaps)
        return std::unexpected{custom_error(fmt::format(fmt::runtime(tr("tui.filters.custom.error.too_long")), fmt::arg("max", kMaxCustomTaps)))};
    std::vector<std::complex<double>> poly{1.0};
    for (const auto& r : zeros) {
        poly.push_back(0.0);
        for (std::size_t k = poly.size() - 1; k > 0; --k) poly[k] -= r * poly[k - 1];
    }
    double sum = 0, alternating = 0, peak = 0;
    for (std::size_t k = 0; k < poly.size(); ++k) {
        out.h.coeffs.push_back(poly[k].real());
        sum += poly[k].real();
        alternating += (k % 2 ? -1.0 : 1.0) * poly[k].real();
        peak = std::max(peak, std::abs(poly[k].real()));
    }
    const double scale = std::abs(sum) > 1e-12 * peak ? sum : std::abs(alternating) > 1e-12 * peak ? alternating : peak;
    for (auto& v : out.h.coeffs) {
        v /= scale;
        if (!std::isfinite(v)) return std::unexpected{custom_error(tr("tui.filters.custom.error.overflow"))};
    }
    return out;
}

std::filesystem::path user_filters_dir(const EnvLookup& env) {
    if (auto xdg = env("XDG_CONFIG_HOME"); xdg && !xdg->empty()) return std::filesystem::path{*xdg} / "signals" / "filters";
#ifdef _WIN32
    if (auto appdata = env("APPDATA"); appdata && !appdata->empty()) return std::filesystem::path{*appdata} / "signals" / "filters";
#endif
    if (auto home = env("HOME"); home && !home->empty()) return std::filesystem::path{*home} / ".config" / "signals" / "filters";
    return std::filesystem::path{"signals-filters"};
}

core::Result<std::filesystem::path> save_user_bank(const std::filesystem::path& dir, UserBank& bank) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return core::make_error(core::Errc::io_error, "cannot create " + dir.string() + ": " + ec.message());
    std::set<std::string, std::less<>> taken;
    for (const auto& b : load_user_banks(dir).banks) taken.insert(b.name);
    const auto base = bank.name;
    const auto stem = file_stem(base);
    auto path = dir / (stem + ".flt");
    for (int n = 2; taken.contains(bank.name) || std::filesystem::exists(path, ec); ++n) {
        bank.name = base + "-" + std::to_string(n);
        path = dir / (stem + "-" + std::to_string(n) + ".flt");
    }
    std::ofstream out{path, std::ios::binary};
    if (!out) return core::make_error(core::Errc::io_error, "cannot write " + path.string());
    out << "name = " << toml_string(bank.name) << "\n"
        << "kind = \"bank\"\n"
        << "h0_offset = " << bank.h0.offset << "\n"
        << "h0 = " << array(bank.h0.coeffs) << "\n"
        << "f0_offset = " << bank.f0.offset << "\n"
        << "f0 = " << array(bank.f0.coeffs) << "\n"
        << "note = " << toml_string(bank.note) << "\n";
    if (!out) return core::make_error(core::Errc::io_error, "cannot write " + path.string());
    return path;
}

core::Result<std::filesystem::path> save_user_filter(const std::filesystem::path& dir, UserFilter& filter) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return core::make_error(core::Errc::io_error, "cannot create " + dir.string() + ": " + ec.message());
    std::set<std::string, std::less<>> taken;
    for (const auto& f : load_user_banks(dir).filters) taken.insert(f.name);
    const auto base = filter.name;
    const auto stem = file_stem(base);
    auto path = dir / (stem + ".flt");
    for (int n = 2; taken.contains(filter.name) || std::filesystem::exists(path, ec); ++n) {
        filter.name = base + "-" + std::to_string(n);
        path = dir / (stem + "-" + std::to_string(n) + ".flt");
    }
    std::ofstream out{path, std::ios::binary};
    if (!out) return core::make_error(core::Errc::io_error, "cannot write " + path.string());
    out << "name = " << toml_string(filter.name) << "\n"
        << "kind = \"filter\"\n"
        << "h_offset = " << filter.h.offset << "\n"
        << "h = " << array(filter.h.coeffs) << "\n"
        << "note = " << toml_string(filter.note) << "\n";
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
            if (t["kind"].value_or(std::string{"bank"}) == "filter") {
                auto h = read_filter(t, "h");
                if (!h) {
                    lib.errors.push_back(f.filename().string() + ": " + h.error().message);
                    continue;
                }
                lib.filters.push_back({t["name"].value_or(f.stem().string()), std::move(*h), t["note"].value_or(std::string{})});
                continue;
            }
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

void set_user_filters(std::vector<UserFilter> filters) {
    auto next = std::make_shared<const std::vector<UserFilter>>(std::move(filters));
    std::lock_guard lock{g_mutex};
    g_filters = std::move(next);
}

std::vector<UserFilter> user_filters() {
    std::lock_guard lock{g_mutex};
    return *g_filters;
}

}  // namespace signals::tui
