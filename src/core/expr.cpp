#include "core/expr.hpp"

#include <algorithm>
#include <cctype>
#include <locale>
#include <sstream>
#include <cmath>
#include <numbers>

namespace signals::core {

namespace {

enum class Tok { number, name, op, end };

struct Token {
    Tok kind = Tok::end;
    std::string text = {};
    double value = 0;
    std::size_t pos = 0;
};

bool name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }
bool digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

std::size_t codepoint_length(std::string_view s, std::size_t i) {
    const auto c = static_cast<unsigned char>(s[i]);
    const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    return std::min(n, s.size() - i);
}

ExprResult<std::vector<Token>> tokenize(std::string_view s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (digit(c) || (c == '.' && i + 1 < s.size() && digit(s[i + 1]))) {
            std::size_t j = i;
            while (j < s.size() && (digit(s[j]) || s[j] == '.')) ++j;
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                std::size_t k = j + 1;
                if (k < s.size() && (s[k] == '+' || s[k] == '-')) ++k;
                if (k < s.size() && digit(s[k])) {
                    while (k < s.size() && digit(s[k])) ++k;
                    j = k;
                }
            }
            double v = 0;
            const auto text = std::string{s.substr(i, j - i)};
            std::istringstream in{text};
            in.imbue(std::locale::classic());
            if (!(in >> v) || in.peek() != std::char_traits<char>::eof()) return std::unexpected{ExprError{ExprError::Kind::unexpected, i, text}};
            out.push_back({Tok::number, text, v, i});
            i = j;
            continue;
        }
        if (name_start(c)) {
            std::size_t j = i;
            while (j < s.size() && name_char(s[j])) ++j;
            out.push_back({Tok::name, std::string{s.substr(i, j - i)}, 0, i});
            i = j;
            continue;
        }
        static constexpr std::string_view two[] = {"<=", ">=", "==", "!=", "&&", "||"};
        bool matched = false;
        for (auto op : two)
            if (s.substr(i, 2) == op) {
                out.push_back({Tok::op, std::string{op}, 0, i});
                i += 2;
                matched = true;
                break;
            }
        if (matched) continue;
        if (std::string_view{"+-*/^(),<>!"}.find(c) != std::string_view::npos) {
            out.push_back({Tok::op, std::string(1, c), 0, i});
            ++i;
            continue;
        }
        return std::unexpected{ExprError{ExprError::Kind::unexpected, i, std::string{s.substr(i, codepoint_length(s, i))}}};
    }
    out.push_back({Tok::end, {}, 0, s.size()});
    return out;
}

class Parser {
public:
    Parser(std::vector<Token> tokens) : t_{std::move(tokens)} {}

    ExprResult<Expr> run() {
        if (t_.front().kind == Tok::end) return std::unexpected{ExprError{ExprError::Kind::empty, 0, {}}};
        auto root = parse_or();
        if (!root) return std::unexpected{root.error()};
        if (peek().kind != Tok::end) return std::unexpected{unexpected()};
        e_.root = *root;
        return std::move(e_);
    }

private:
    using Op = Expr::Op;

    const Token& peek() const { return t_[i_]; }
    bool is(std::string_view op) const { return peek().kind == Tok::op && peek().text == op; }
    ExprError unexpected() const {
        const auto& k = peek();
        if (k.kind == Tok::end) return {ExprError::Kind::unexpected_end, k.pos, {}};
        return {ExprError::Kind::unexpected, k.pos, k.text};
    }
    std::size_t add(Expr::Node n) {
        e_.nodes.push_back(std::move(n));
        return e_.nodes.size() - 1;
    }

    template <class Next>
    ExprResult<std::size_t> binary(std::initializer_list<std::pair<std::string_view, Op>> ops, Next next) {
        auto lhs = (this->*next)();
        if (!lhs) return lhs;
        for (;;) {
            const auto it = std::find_if(ops.begin(), ops.end(), [&](const auto& p) { return is(p.first); });
            if (it == ops.end()) return lhs;
            const auto pos = peek().pos;
            ++i_;
            auto rhs = (this->*next)();
            if (!rhs) return rhs;
            lhs = add({it->second, 0, {}, pos, {*lhs, *rhs}});
        }
    }

    ExprResult<std::size_t> parse_or() { return binary({{"||", Op::or_}}, &Parser::parse_and); }
    ExprResult<std::size_t> parse_and() { return binary({{"&&", Op::and_}}, &Parser::parse_eq); }
    ExprResult<std::size_t> parse_eq() { return binary({{"==", Op::eq}, {"!=", Op::ne}}, &Parser::parse_cmp); }
    ExprResult<std::size_t> parse_cmp() {
        return binary({{"<=", Op::le}, {">=", Op::ge}, {"<", Op::lt}, {">", Op::gt}}, &Parser::parse_add);
    }
    ExprResult<std::size_t> parse_add() { return binary({{"+", Op::add}, {"-", Op::sub}}, &Parser::parse_mul); }
    ExprResult<std::size_t> parse_mul() { return binary({{"*", Op::mul}, {"/", Op::div}}, &Parser::parse_unary); }

    ExprResult<std::size_t> parse_unary() {
        for (auto [text, op] : {std::pair{"-", Op::neg}, std::pair{"+", Op::plus}, std::pair{"!", Op::not_}})
            if (is(text)) {
                const auto pos = peek().pos;
                ++i_;
                auto arg = parse_unary();
                if (!arg) return arg;
                return add({op, 0, {}, pos, {*arg}});
            }
        return parse_pow();
    }

    ExprResult<std::size_t> parse_pow() {
        auto base = parse_primary();
        if (!base || !is("^")) return base;
        const auto pos = peek().pos;
        ++i_;
        auto exponent = parse_unary();
        if (!exponent) return exponent;
        return add({Op::pow, 0, {}, pos, {*base, *exponent}});
    }

    ExprResult<std::size_t> parse_primary() {
        const auto tok = peek();
        if (tok.kind == Tok::number) {
            ++i_;
            return add({Op::number, tok.value, {}, tok.pos, {}});
        }
        if (tok.kind == Tok::name) {
            ++i_;
            if (!is("(")) {
                if (tok.text == "pi") return add({Op::number, std::numbers::pi, {}, tok.pos, {}});
                return add({Op::name, 0, tok.text, tok.pos, {}});
            }
            static constexpr std::string_view unary[] = {"abs", "sqrt", "log10"};
            const bool one = std::ranges::find(unary, tok.text) != std::end(unary);
            if (!one && tok.text != "min" && tok.text != "max")
                return std::unexpected{ExprError{ExprError::Kind::unknown_function, tok.pos, tok.text}};
            ++i_;
            std::vector<std::size_t> args;
            if (!is(")"))
                for (;;) {
                    auto a = parse_or();
                    if (!a) return a;
                    args.push_back(*a);
                    if (!is(",")) break;
                    ++i_;
                }
            if (!is(")")) return std::unexpected{unexpected()};
            ++i_;
            if ((one && args.size() != 1) || args.empty())
                return std::unexpected{ExprError{ExprError::Kind::bad_arguments, tok.pos, tok.text}};
            return add({Op::call, 0, tok.text, tok.pos, std::move(args)});
        }
        if (is("(")) {
            ++i_;
            auto inner = parse_or();
            if (!inner) return inner;
            if (!is(")")) return std::unexpected{unexpected()};
            ++i_;
            return inner;
        }
        return std::unexpected{unexpected()};
    }

    std::vector<Token> t_;
    std::size_t i_ = 0;
    Expr e_;
};

ExprResult<double> eval_node(const Expr& e, std::size_t i, const NameLookup& names) {
    const auto& n = e.nodes[i];
    if (n.op == Expr::Op::number) return n.value;
    if (n.op == Expr::Op::name) {
        auto v = names ? names(n.name) : std::nullopt;
        if (!v) return std::unexpected{ExprError{ExprError::Kind::unknown_name, n.pos, n.name}};
        return *v;
    }
    std::vector<double> a;
    a.reserve(n.args.size());
    for (auto k : n.args) {
        auto v = eval_node(e, k, names);
        if (!v) return v;
        a.push_back(*v);
    }
    const auto truth = [](bool b) { return b ? 1.0 : 0.0; };
    switch (n.op) {
    case Expr::Op::neg: return -a[0];
    case Expr::Op::plus: return a[0];
    case Expr::Op::not_: return truth(a[0] == 0);
    case Expr::Op::add: return a[0] + a[1];
    case Expr::Op::sub: return a[0] - a[1];
    case Expr::Op::mul: return a[0] * a[1];
    case Expr::Op::div: return a[0] / a[1];
    case Expr::Op::pow: return std::pow(a[0], a[1]);
    case Expr::Op::lt: return truth(a[0] < a[1]);
    case Expr::Op::le: return truth(a[0] <= a[1]);
    case Expr::Op::gt: return truth(a[0] > a[1]);
    case Expr::Op::ge: return truth(a[0] >= a[1]);
    case Expr::Op::eq: return truth(a[0] == a[1]);
    case Expr::Op::ne: return truth(a[0] != a[1]);
    case Expr::Op::and_: return truth(a[0] != 0 && a[1] != 0);
    case Expr::Op::or_: return truth(a[0] != 0 || a[1] != 0);
    case Expr::Op::call:
        if (n.name == "abs") return std::abs(a[0]);
        if (n.name == "sqrt") return std::sqrt(a[0]);
        if (n.name == "log10") return std::log10(a[0]);
        if (n.name == "min") return *std::ranges::min_element(a);
        return *std::ranges::max_element(a);
    case Expr::Op::number:
    case Expr::Op::name: break;
    }
    return 0.0;
}

}  // namespace

ExprResult<Expr> parse_expr(std::string_view text) {
    auto tokens = tokenize(text);
    if (!tokens) return std::unexpected{tokens.error()};
    return Parser{std::move(*tokens)}.run();
}

ExprResult<double> eval(const Expr& e, const NameLookup& names) {
    if (e.nodes.empty()) return std::unexpected{ExprError{ExprError::Kind::empty, 0, {}}};
    return eval_node(e, e.root, names);
}

ExprResult<double> eval_number(std::string_view text) {
    auto e = parse_expr(text);
    if (!e) return std::unexpected{e.error()};
    return eval(*e, {});
}

std::vector<std::string> expr_names(const Expr& e) {
    std::vector<std::string> out;
    std::vector<std::pair<std::size_t, std::string>> found;
    for (std::size_t i = 0; i < e.nodes.size(); ++i)
        if (e.nodes[i].op == Expr::Op::name) found.emplace_back(e.nodes[i].pos, e.nodes[i].name);
    std::ranges::sort(found);
    for (auto& [pos, name] : found)
        if (std::ranges::find(out, name) == out.end()) out.push_back(name);
    return out;
}

}  // namespace signals::core
