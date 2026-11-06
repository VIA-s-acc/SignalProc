#include "theory/live.hpp"

#include <algorithm>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::theory {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

}  // namespace

void LiveValues::set(std::string name, std::string text) { values_[std::move(name)].text = std::move(text); }

void LiveValues::set_check(core::CheckResult r) {
    auto name = r.id;
    values_[std::move(name)].check = std::move(r);
}

const LiveValue* LiveValues::find(std::string_view name) const {
    const auto it = values_.find(name);
    return it == values_.end() ? nullptr : &it->second;
}

std::vector<std::string> placeholders(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t i = text.find("{{"); i != std::string_view::npos; i = text.find("{{", i)) {
        const auto close = text.find("}}", i + 2);
        if (close == std::string_view::npos) break;
        std::string name{trim(text.substr(i + 2, close - i - 2))};
        if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(std::move(name));
        i = close + 2;
    }
    return out;
}

std::string substitute(std::string_view text, const LiveValues& v, Context c, const std::function<std::string(double)>& number) {
    std::string out;
    std::size_t at = 0;
    for (std::size_t i = text.find("{{"); i != std::string_view::npos; i = text.find("{{", at)) {
        const auto close = text.find("}}", i + 2);
        if (close == std::string_view::npos) break;
        out += text.substr(at, i - at);
        const auto name = trim(text.substr(i + 2, close - i - 2));
        const auto* value = v.find(name);
        if (value && value->check) {
            const auto& r = *value->check;
            if (c == Context::math) out += number(r.value);
            else if (r.passed()) out += "✓ " + number(r.value);
            else out += "✗ " + number(r.value) + " (" + fmt::format(fmt::runtime(tr("theory.threshold")), fmt::arg("value", number(r.tolerance))) + ")";
        } else if (value) {
            out += value->text;
        } else if (c == Context::prose && name.starts_with("check.")) {
            out += tr("theory.no_data");
        } else {
            out += "—";
        }
        at = close + 2;
    }
    out += text.substr(at);
    return out;
}

}  // namespace signals::theory
