#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/check.hpp"

namespace signals::theory {

struct LiveValue {
    std::string text;
    std::optional<core::CheckResult> check;
};

class LiveValues {
public:
    void set(std::string name, std::string text);
    void set_check(core::CheckResult r);
    const LiveValue* find(std::string_view name) const;
    bool empty() const { return values_.empty(); }

    std::string source;
    bool snapshot = false;

private:
    std::map<std::string, LiveValue, std::less<>> values_;
};

std::vector<std::string> placeholders(std::string_view text);

enum class Context { prose, math };

std::string substitute(std::string_view text, const LiveValues& v, Context c, const std::function<std::string(double)>& number);

}  // namespace signals::theory
