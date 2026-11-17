#include "pipeline/flatten.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <vector>

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::pipeline {
namespace {

using Kind = ParamSpec::Kind;

constexpr std::size_t kMaxExpanded = 64 * 1024;
constexpr std::size_t kMaxDepth = 64;
constexpr std::size_t kMaxTotal = 8 * 1024 * 1024;

bool name_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool name_char(char c) { return name_start(c) || (c >= '0' && c <= '9'); }

bool plain_number(std::string_view s) {
    if (s.empty() || s.front() == ' ') return false;
    const std::string text{s};
    char* end = nullptr;
    std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size();
}

bool numeric(Kind k) { return k == Kind::integer || k == Kind::real; }
bool textual(Kind k) { return k == Kind::text || k == Kind::path || k == Kind::gains; }

core::Error fault(std::string message) { return {core::Errc::invalid_argument, std::move(message)}; }

std::string unknown(std::string_view name, const Scope& scope) {
    std::vector<std::string> names;
    for (const auto& [n, v] : scope.values) names.push_back(n);
    const auto hint = nearest_name(name, names);
    return hint ? fmt::format(fmt::runtime(tr("pipeline.vars.unknown_hint")), fmt::arg("name", name), fmt::arg("hint", *hint))
                : fmt::format(fmt::runtime(tr("pipeline.vars.unknown")), fmt::arg("name", name));
}

struct Memo {
    std::string text;
    std::size_t depth = 0;
};

struct Expander {
    explicit Expander(const Scope& s) : scope{s} {}

    const Scope& scope;
    std::size_t budget = kMaxTotal;
    std::map<Kind, std::map<std::string, Memo, std::less<>>> memo;
    std::vector<std::string> stack;

    core::Result<const Memo*> resolve(const std::string& name, Kind kind);
    core::Result<std::string> expand(std::string_view value, Kind kind, std::size_t& depth);
};

core::Error too_deep() { return fault(fmt::format(fmt::runtime(tr("pipeline.vars.too_deep")), fmt::arg("limit", kMaxDepth))); }

core::Result<const Memo*> Expander::resolve(const std::string& name, Kind kind) {
    auto& table = memo[kind];
    if (const auto hit = table.find(name); hit != table.end()) {
        if (stack.size() + hit->second.depth > kMaxDepth) return std::unexpected{too_deep()};
        return &hit->second;
    }
    const auto it = scope.values.find(name);
    if (it == scope.values.end()) return std::unexpected{fault(unknown(name, scope))};
    if (stack.size() >= kMaxDepth) return std::unexpected{too_deep()};
    if (std::ranges::find(stack, name) != stack.end()) {
        std::string chain;
        for (const auto& s : stack) chain += s + " → ";
        return std::unexpected{fault(fmt::format(fmt::runtime(tr("pipeline.vars.cycle")), fmt::arg("chain", chain + name)))};
    }
    stack.push_back(name);
    std::size_t depth = 0;
    auto text = expand(it->second, kind, depth);
    stack.pop_back();
    if (!text) return std::unexpected{text.error()};
    return &table.emplace(name, Memo{std::move(*text), depth + 1}).first->second;
}

core::Result<std::string> Expander::expand(std::string_view value, Kind kind, std::size_t& depth) {
    std::string out;
    std::size_t names = 0, at = 0;
    bool other = false;
    const std::string* only = nullptr;
    while (at < value.size()) {
        if (value[at] != '$') {
            out += value[at++];
            other = true;
            continue;
        }
        if (at + 1 < value.size() && value[at + 1] == '$') {
            out += '$';
            at += 2;
            other = true;
            continue;
        }
        auto end = at + 1;
        if (end >= value.size() || !name_start(value[end])) return std::unexpected{fault(tr("pipeline.vars.bad_dollar"))};
        while (end < value.size() && name_char(value[end])) ++end;
        const auto entry = resolve(std::string{value.substr(at + 1, end - at - 1)}, kind);
        if (!entry) return std::unexpected{entry.error()};
        const auto& inner = (*entry)->text;
        depth = std::max(depth, (*entry)->depth);
        ++names;
        only = &inner;
        const bool wrap = numeric(kind) && !plain_number(inner);
        const auto size = inner.size() + (wrap ? 2 : 0);
        if (size > budget) return std::unexpected{fault(fmt::format(fmt::runtime(tr("pipeline.vars.too_much")), fmt::arg("limit", kMaxTotal)))};
        budget -= size;
        if (wrap) out += '(';
        out += inner;
        if (wrap) out += ')';
        if (out.size() > kMaxExpanded) return std::unexpected{fault(fmt::format(fmt::runtime(tr("pipeline.vars.too_long")), fmt::arg("limit", kMaxExpanded)))};
        at = end;
    }
    if (names == 1 && !other) return *only;
    if (names > 0 && !numeric(kind) && !textual(kind)) return std::unexpected{fault(tr("pipeline.vars.expr_kind"))};
    return out;
}

}  // namespace

std::expected<Scope, SigError> resolve_lets(std::span<const LetDecl> lets) {
    Scope scope;
    for (const auto& l : lets) scope.values.emplace(l.name, l.value);
    Expander expander{scope};
    for (const auto& l : lets)
        if (auto r = expander.resolve(l.name, Kind::real); !r) return std::unexpected{SigError{l.line, 0, "let " + l.name + ": " + r.error().message}};
    return scope;
}

core::Result<std::string> substitute(std::string_view value, const Scope& scope, Kind kind) {
    Expander expander{scope};
    std::size_t depth = 0;
    return expander.expand(value, kind, depth);
}

std::expected<PipelineDoc, SigError> flatten(const PipelineDoc& doc, const FlattenOptions&) {
    PipelineDoc out = doc;
    for (auto& n : out.nodes)
        if (n.source.empty()) n.source = n.params;
    if (doc.version < 2) return out;
    const auto scope = resolve_lets(doc.lets);
    if (!scope) return std::unexpected{scope.error()};
    out.lets.clear();
    Expander expander{*scope};
    for (auto& n : out.nodes) {
        const auto* type = find_block_type(n.type);
        for (auto& [param, value] : n.params) {
            Kind kind = Kind::text;
            if (type)
                if (const auto spec = std::ranges::find_if(type->params, [&](const ParamSpec& p) { return p.name == param; }); spec != type->params.end()) kind = spec->kind;
            std::size_t depth = 0;
            auto done = expander.expand(value, kind, depth);
            if (!done) return std::unexpected{SigError{n.line, 0, n.id + "." + param + ": " + done.error().message}};
            value = std::move(*done);
        }
    }
    return out;
}

}  // namespace signals::pipeline
