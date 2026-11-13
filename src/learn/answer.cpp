#include "learn/answer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/expr_text.hpp"

namespace signals::learn {

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string{s};
}

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::unexpected<core::Error> fail(std::string message) { return core::make_error(core::Errc::invalid_argument, std::move(message)); }

std::string strip_brackets(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && ((s.front() == '[' && s.back() == ']') || (s.front() == '(' && s.back() == ')'))) {
        int depth = 0;
        for (std::size_t i = 0; i + 1 < s.size(); ++i) {
            depth += s[i] == '(' || s[i] == '[';
            depth -= s[i] == ')' || s[i] == ']';
            if (depth == 0) return s;
        }
        return trim(std::string_view{s}.substr(1, s.size() - 2));
    }
    return s;
}

std::vector<std::string> split_top(std::string_view s, std::string_view separators) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '[') ++depth;
        if (c == ')' || c == ']') --depth;
        if (depth == 0 && separators.find(c) != std::string_view::npos) {
            if (!trim(cur).empty()) out.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

bool has_top(std::string_view s, std::string_view separators) {
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '[') ++depth;
        if (c == ')' || c == ']') --depth;
        if (depth == 0 && separators.find(c) != std::string_view::npos) return true;
    }
    return false;
}

core::Result<double> number(const std::string& token) {
    auto v = core::eval_number(token);
    if (!v) return fail(fmt::format(fmt::runtime(tr("learn.answer.bad_number")), fmt::arg("text", token), fmt::arg("why", describe(v.error()))));
    return *v;
}

}  // namespace

std::string describe(const core::ExprError& e) { return pipeline::describe_expr_error(e); }

core::Result<std::vector<double>> parse_answer(std::string_view text, const Answer& a) {
    auto body = strip_brackets(std::string{text});
    if (body.empty()) return fail(tr("learn.answer.empty"));
    if (a.type == AnswerType::choice) {
        for (std::size_t i = 0; i < a.options.size(); ++i)
            if (lower(trim(a.options[i])) == lower(body)) return std::vector<double>{static_cast<double>(i + 1)};
        auto v = core::eval_number(body);
        if (v && *v == std::floor(*v) && *v >= 1 && *v <= static_cast<double>(a.options.size())) return std::vector<double>{*v};
        return fail(fmt::format(fmt::runtime(tr("learn.answer.bad_choice")), fmt::arg("count", a.options.size())));
    }
    if (a.type != AnswerType::vector) {
        if (body.find('.') == std::string::npos && has_top(body, ",")) std::ranges::replace(body, ',', '.');
        auto v = number(body);
        if (!v) return std::unexpected{v.error()};
        if (a.type == AnswerType::integer && std::abs(*v - std::round(*v)) > 1e-9) return fail(tr("learn.answer.not_integer"));
        return std::vector<double>{*v};
    }
    const auto tokens = has_top(body, ",;") ? split_top(body, ",;") : split_top(body, " \t\n");
    std::vector<double> out;
    for (const auto& t : tokens) {
        auto v = number(t);
        if (!v) return std::unexpected{v.error()};
        out.push_back(*v);
    }
    if (out.empty()) return fail(tr("learn.answer.empty"));
    return out;
}

AnswerCheck compare_answer(std::span<const double> given, std::span<const double> expected, const Answer& a) {
    if (given.size() != expected.size())
        return {false, fmt::format(fmt::runtime(tr("learn.answer.length")), fmt::arg("need", expected.size()), fmt::arg("got", given.size()))};
    for (std::size_t i = 0; i < given.size(); ++i) {
        const double limit = a.relative ? a.tol * std::abs(expected[i]) : a.tol;
        if (!(std::abs(given[i] - expected[i]) <= limit + 1e-12 * std::max(1.0, std::abs(expected[i])))) {
            if (a.type == AnswerType::choice) return {false, tr("learn.answer.wrong_choice")};
            if (given.size() == 1) return {false, tr("learn.answer.wrong_one")};
            return {false, fmt::format(fmt::runtime(tr("learn.answer.wrong")), fmt::arg("index", i + 1))};
        }
    }
    return {true, tr("learn.answer.right")};
}

std::string format_answer(std::span<const double> values, const Answer& a) {
    if (a.type == AnswerType::choice && values.size() == 1) {
        const auto i = static_cast<std::size_t>(values[0]);
        if (i >= 1 && i <= a.options.size()) return a.options[i - 1];
    }
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) out += (i ? " " : "") + fmt::format("{:.10g}", values[i]);
    return out;
}

}  // namespace signals::learn
