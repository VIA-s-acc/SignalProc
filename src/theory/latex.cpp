#include "theory/latex.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <optional>
#include <utility>

#include "theory/text.hpp"

namespace signals::theory {
namespace {

struct Glyph {
    std::string_view command, unicode, ascii;
};

constexpr std::array kSymbols{
    Glyph{"alpha", "α", "alpha"},      Glyph{"beta", "β", "beta"},        Glyph{"gamma", "γ", "gamma"},
    Glyph{"delta", "δ", "delta"},      Glyph{"epsilon", "ϵ", "eps"},      Glyph{"varepsilon", "ε", "eps"},
    Glyph{"zeta", "ζ", "zeta"},        Glyph{"eta", "η", "eta"},          Glyph{"theta", "θ", "theta"},
    Glyph{"iota", "ι", "iota"},        Glyph{"kappa", "κ", "kappa"},      Glyph{"lambda", "λ", "lambda"},
    Glyph{"mu", "μ", "mu"},            Glyph{"nu", "ν", "nu"},            Glyph{"xi", "ξ", "xi"},
    Glyph{"pi", "π", "pi"},            Glyph{"rho", "ρ", "rho"},          Glyph{"sigma", "σ", "sigma"},
    Glyph{"tau", "τ", "tau"},          Glyph{"upsilon", "υ", "upsilon"},  Glyph{"phi", "ϕ", "phi"},
    Glyph{"varphi", "φ", "phi"},       Glyph{"chi", "χ", "chi"},          Glyph{"psi", "ψ", "psi"},
    Glyph{"omega", "ω", "omega"},      Glyph{"Gamma", "Γ", "Gamma"},      Glyph{"Delta", "Δ", "Delta"},
    Glyph{"Theta", "Θ", "Theta"},      Glyph{"Lambda", "Λ", "Lambda"},    Glyph{"Xi", "Ξ", "Xi"},
    Glyph{"Pi", "Π", "Pi"},            Glyph{"Sigma", "Σ", "Sigma"},      Glyph{"Phi", "Φ", "Phi"},
    Glyph{"Psi", "Ψ", "Psi"},          Glyph{"Omega", "Ω", "Omega"},      Glyph{"ell", "ℓ", "l"},
    Glyph{"infty", "∞", "inf"},        Glyph{"partial", "∂", "d"},        Glyph{"dots", "…", "..."},
    Glyph{"ldots", "…", "..."},        Glyph{"cdots", "⋯", "..."},        Glyph{"approx", "≈", "~="},
    Glyph{"le", "≤", "<="},            Glyph{"leq", "≤", "<="},           Glyph{"ge", "≥", ">="},
    Glyph{"geq", "≥", ">="},           Glyph{"ne", "≠", "!="},            Glyph{"neq", "≠", "!="},
    Glyph{"sim", "∼", "~"},            Glyph{"propto", "∝", "~"},         Glyph{"equiv", "≡", "=="},
    Glyph{"in", "∈", " in "},          Glyph{"notin", "∉", " not in "},   Glyph{"subset", "⊂", " subset "},
    Glyph{"subseteq", "⊆", " subseteq "}, Glyph{"to", "→", "->"},         Glyph{"iff", "⇔", "<=>"},
    Glyph{"Rightarrow", "⇒", "=>"},    Glyph{"leftarrow", "←", "<-"},     Glyph{"leftrightarrow", "↔", "<->"},
    Glyph{"downarrow", "↓", "v"},      Glyph{"uparrow", "↑", "^"},        Glyph{"mid", "∣", "|"},
    Glyph{"pm", "±", "+-"},            Glyph{"times", "×", "x"},          Glyph{"cdot", "·", "*"},
    Glyph{"otimes", "⊗", "(x)"},       Glyph{"oplus", "⊕", "(+)"},        Glyph{"circledast", "⊛", "(*)"},
    Glyph{"cup", "∪", "U"},            Glyph{"cap", "∩", "^"},            Glyph{"lfloor", "⌊", "|_"},
    Glyph{"rfloor", "⌋", "_|"},        Glyph{"lceil", "⌈", "|^"},         Glyph{"rceil", "⌉", "^|"},
    Glyph{"lvert", "|", "|"},          Glyph{"rvert", "|", "|"},          Glyph{"langle", "⟨", "<"},
    Glyph{"rangle", "⟩", ">"},         Glyph{"|", "‖", "||"},             Glyph{"{", "{", "{"},
    Glyph{"}", "}", "}"},              Glyph{"#", "#", "#"},              Glyph{"%", "%", "%"},
    Glyph{"_", "_", "_"},              Glyph{"&", "&", "&"},              Glyph{"$", "$", "$"},
};

constexpr std::array kBigOps{
    Glyph{"sum", "∑", "sum"}, Glyph{"prod", "∏", "prod"}, Glyph{"int", "∫", "int"}, Glyph{"iint", "∬", "iint"},
    Glyph{"bigcup", "⋃", "U"}, Glyph{"bigcap", "⋂", "^"},
};

constexpr std::array<std::string_view, 21> kFunctions{"cos", "sin", "tan", "cot", "log", "lg", "ln", "exp", "det", "max", "min",
                                                     "sup", "inf", "lim", "arg", "gcd", "deg", "sinh", "cosh", "tanh", "dim"};
constexpr std::array<std::string_view, 5> kLimitFunctions{"max", "min", "sup", "inf", "lim"};

struct Space {
    std::string_view command, text;
};
constexpr std::array kSpaces{Space{",", " "}, Space{";", " "}, Space{":", " "}, Space{"!", ""}, Space{" ", " "},
                             Space{"quad", "  "}, Space{"qquad", "    "}, Space{"textstyle", ""}, Space{"displaystyle", ""}};

constexpr std::array<std::string_view, 4> kAccents{"hat", "tilde", "bar", "overline"};
constexpr std::array<std::string_view, 5> kFonts{"text", "mathrm", "mathsf", "mathbf", "operatorname"};

template <class A>
const Glyph* lookup(const A& table, std::string_view name) {
    for (const auto& g : table)
        if (g.command == name) return &g;
    return nullptr;
}

std::string double_struck(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case 'R': out += "ℝ"; break;
        case 'Z': out += "ℤ"; break;
        case 'N': out += "ℕ"; break;
        case 'C': out += "ℂ"; break;
        case 'Q': out += "ℚ"; break;
        case 'E': out += "𝔼"; break;
        default: out += c;
        }
    }
    return out;
}

std::string calligraphic(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case 'B': out += "ℬ"; break;
        case 'E': out += "ℰ"; break;
        case 'F': out += "ℱ"; break;
        case 'H': out += "ℋ"; break;
        case 'I': out += "ℐ"; break;
        case 'L': out += "ℒ"; break;
        case 'M': out += "ℳ"; break;
        case 'R': out += "ℛ"; break;
        default:
            if (c >= 'A' && c <= 'Z') out += to_utf8(std::u32string(1, static_cast<char32_t>(0x1D49C + (c - 'A'))));
            else out += c;
        }
    }
    return out;
}

MathNode symbol(std::string text, std::string ascii) {
    MathNode n;
    n.kind = MathNode::Kind::symbol;
    n.text = std::move(text);
    n.ascii = std::move(ascii);
    return n;
}

class Parser {
public:
    explicit Parser(std::string_view s) : s_{s} {}

    std::expected<MathNode, LatexError> run() {
        auto row = parse_row(Stop::end);
        if (!row) return std::unexpected{row.error()};
        if (pos_ < s_.size()) return fail(pos_, "unexpected '" + std::string{s_[pos_]} + "'");
        return row;
    }

private:
    enum class Stop { end, brace, bracket, right, env };

    std::unexpected<LatexError> fail(std::size_t at, std::string message) const { return std::unexpected{LatexError{at, std::move(message)}}; }

    void skip_spaces() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    std::string read_command() {
        std::size_t start = pos_;
        if (pos_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[pos_]))) {
            while (pos_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        } else if (pos_ < s_.size()) {
            ++pos_;
        }
        return std::string{s_.substr(start, pos_ - start)};
    }

    bool at_command(std::string_view name) const {
        if (pos_ >= s_.size() || s_[pos_] != '\\' || s_.compare(pos_ + 1, name.size(), name) != 0) return false;
        const auto after = pos_ + 1 + name.size();
        return after >= s_.size() || !std::isalpha(static_cast<unsigned char>(s_[after]));
    }

    std::expected<std::string, LatexError> raw_group() {
        skip_spaces();
        if (pos_ >= s_.size() || s_[pos_] != '{') {
            if (pos_ < s_.size()) return std::string{s_[pos_++]};
            return fail(pos_, "missing argument");
        }
        const auto start = ++pos_;
        int depth = 1;
        while (pos_ < s_.size()) {
            if (s_[pos_] == '\\') {
                pos_ += 2;
                continue;
            }
            if (s_[pos_] == '{') ++depth;
            if (s_[pos_] == '}' && --depth == 0) break;
            ++pos_;
        }
        if (pos_ >= s_.size()) return fail(start - 1, "unbalanced '{'");
        return std::string{s_.substr(start, pos_++ - start)};
    }

    std::expected<MathNode, LatexError> argument() {
        skip_spaces();
        if (pos_ >= s_.size()) return fail(pos_, "missing argument");
        if (s_[pos_] == '}' || s_[pos_] == '&') return fail(pos_, "missing argument");
        auto a = atom();
        if (a && a->kind == MathNode::Kind::row && a->children.size() == 1) {
            MathNode only = std::move(a->children.front());
            return only;
        }
        return a;
    }

    std::expected<MathNode, LatexError> delimiter() {
        skip_spaces();
        if (pos_ >= s_.size()) return fail(pos_, "missing delimiter");
        if (s_[pos_] == '.') {
            ++pos_;
            return symbol("", "");
        }
        if (s_[pos_] == '\\') {
            const auto at = pos_++;
            const auto name = read_command();
            if (const auto* g = lookup(kSymbols, name)) return symbol(std::string{g->unicode}, std::string{g->ascii});
            return fail(at, "unknown delimiter \\" + name);
        }
        const auto c = to_u32(s_.substr(pos_, 4)).front();
        const auto bytes = to_utf8(std::u32string(1, c));
        pos_ += bytes.size();
        return symbol(bytes, bytes);
    }

    std::expected<MathNode, LatexError> environment(std::size_t at) {
        auto name = raw_group();
        if (!name) return std::unexpected{name.error()};
        MathNode m;
        m.kind = MathNode::Kind::matrix;
        m.text = *name;
        if (*name == "pmatrix") m.left = "(", m.right = ")";
        else if (*name == "bmatrix") m.left = "[", m.right = "]";
        else if (*name == "cases") m.left = "{";
        else if (*name != "aligned" && *name != "matrix") return fail(at, "unknown environment " + *name);
        std::vector<std::vector<MathNode>> rows(1);
        while (true) {
            auto cell = parse_row(Stop::env);
            if (!cell) return std::unexpected{cell.error()};
            rows.back().push_back(std::move(*cell));
            if (pos_ >= s_.size()) return fail(at, "missing \\end{" + *name + "}");
            if (s_[pos_] == '&') {
                ++pos_;
                continue;
            }
            if (s_.compare(pos_, 2, "\\\\") == 0) {
                pos_ += 2;
                rows.emplace_back();
                continue;
            }
            if (at_command("end")) {
                pos_ += 4;
                auto end = raw_group();
                if (!end) return std::unexpected{end.error()};
                if (*end != *name) return fail(at, "\\begin{" + *name + "} ends with \\end{" + *end + "}");
                break;
            }
            return fail(pos_, "unexpected input in " + *name);
        }
        if (rows.size() > 1 && rows.back().size() == 1 && rows.back().front().children.empty()) rows.pop_back();
        for (const auto& r : rows) m.columns = std::max(m.columns, r.size());
        for (auto& r : rows) {
            r.resize(m.columns);
            for (auto& c : r) m.children.push_back(std::move(c));
        }
        return m;
    }

    std::expected<MathNode, LatexError> command(std::size_t at) {
        const auto name = read_command();
        if (name.empty()) return fail(at, "lone backslash");
        if (const auto* g = lookup(kSymbols, name)) return symbol(std::string{g->unicode}, std::string{g->ascii});
        for (const auto& sp : kSpaces)
            if (sp.command == name) {
                MathNode n;
                n.kind = MathNode::Kind::space;
                n.text = std::string{sp.text};
                n.ascii = n.text;
                return n;
            }
        if (const auto* g = lookup(kBigOps, name)) {
            MathNode n;
            n.kind = MathNode::Kind::big_op;
            n.text = std::string{g->unicode};
            n.ascii = std::string{g->ascii};
            n.limits = name != "int" && name != "iint";
            return n;
        }
        for (auto f : kFunctions)
            if (f == name) {
                MathNode n;
                n.kind = MathNode::Kind::function;
                n.text = name;
                n.ascii = name;
                for (auto l : kLimitFunctions) n.limits = n.limits || l == name;
                return n;
            }
        if (name == "bmod") {
            MathNode n;
            n.kind = MathNode::Kind::function;
            n.text = n.ascii = "mod";
            return n;
        }
        if (name == "frac" || name == "tfrac" || name == "dfrac" || name == "binom") {
            auto num = argument();
            if (!num) return num;
            auto den = argument();
            if (!den) return den;
            MathNode n;
            n.kind = MathNode::Kind::frac;
            n.text = name;
            n.children = {std::move(*num), std::move(*den)};
            if (name != "binom") return n;
            MathNode f;
            f.kind = MathNode::Kind::fenced;
            f.left = "(", f.right = ")";
            f.children.push_back(std::move(n));
            return f;
        }
        if (name == "sqrt") {
            MathNode n;
            n.kind = MathNode::Kind::sqrt;
            skip_spaces();
            std::optional<MathNode> degree;
            if (pos_ < s_.size() && s_[pos_] == '[') {
                ++pos_;
                auto i = parse_row(Stop::bracket);
                if (!i) return i;
                if (pos_ >= s_.size()) return fail(at, "unbalanced '['");
                ++pos_;
                degree = std::move(*i);
            }
            auto r = argument();
            if (!r) return r;
            n.children.push_back(std::move(*r));
            if (degree) n.children.push_back(std::move(*degree));
            return n;
        }
        for (auto a : kAccents)
            if (a == name) {
                auto arg = argument();
                if (!arg) return arg;
                MathNode n;
                n.kind = MathNode::Kind::accent;
                n.text = name;
                n.children.push_back(std::move(*arg));
                return n;
            }
        for (auto f : kFonts)
            if (f == name) {
                auto content = raw_group();
                if (!content) return std::unexpected{content.error()};
                MathNode n;
                n.kind = name == "operatorname" ? MathNode::Kind::function : MathNode::Kind::text;
                n.text = *content;
                n.ascii = *content;
                return n;
            }
        if (name == "mathbb" || name == "mathcal") {
            auto content = raw_group();
            if (!content) return std::unexpected{content.error()};
            MathNode n;
            n.kind = MathNode::Kind::text;
            n.text = name == "mathbb" ? double_struck(*content) : calligraphic(*content);
            n.ascii = *content;
            return n;
        }
        if (name == "left") {
            auto l = delimiter();
            if (!l) return l;
            auto body = parse_row(Stop::right);
            if (!body) return body;
            if (!at_command("right")) return fail(at, "\\left without \\right");
            pos_ += 6;
            auto r = delimiter();
            if (!r) return r;
            MathNode n;
            n.kind = MathNode::Kind::fenced;
            n.left = l->text;
            n.right = r->text;
            n.size = -1;
            n.children.push_back(std::move(*body));
            return n;
        }
        if (name == "right") return fail(at, "\\right without \\left");
        if (name == "big" || name == "Big" || name == "bigg" || name == "Bigg" || name == "bigl" || name == "bigr" || name == "Bigl" ||
            name == "Bigr") {
            auto d = delimiter();
            if (!d) return d;
            d->size = name.starts_with("Big") ? 2 : 1;
            return d;
        }
        if (name == "begin") return environment(at);
        if (name == "end") return fail(at, "\\end without \\begin");
        return fail(at, "unknown command \\" + name);
    }

    std::expected<MathNode, LatexError> atom() {
        skip_spaces();
        const auto at = pos_;
        const char c = s_[pos_];
        if (c == '{') {
            ++pos_;
            auto row = parse_row(Stop::brace);
            if (!row) return row;
            if (pos_ >= s_.size()) return fail(at, "unbalanced '{'");
            ++pos_;
            return row;
        }
        if (c == '\\') {
            ++pos_;
            return command(at);
        }
        if (c == '\'') {
            ++pos_;
            return symbol("′", "'");
        }
        if (c == '}') return fail(at, "unbalanced '}'");
        const auto cp = to_u32(s_.substr(pos_, 4)).front();
        const auto bytes = to_utf8(std::u32string(1, cp));
        pos_ += bytes.size();
        if (c == '-') return symbol("−", "-");
        if (c == '*') return symbol("∗", "*");
        return symbol(bytes, bytes);
    }

    std::expected<MathNode, LatexError> parse_row(Stop stop) {
        MathNode row;
        while (true) {
            skip_spaces();
            if (pos_ >= s_.size()) {
                if (stop == Stop::brace) return fail(pos_, "unbalanced '{'");
                if (stop == Stop::right) return fail(pos_, "\\left without \\right");
                if (stop == Stop::bracket) return fail(pos_, "unbalanced '['");
                return row;
            }
            const char c = s_[pos_];
            if (c == '}') {
                if (stop == Stop::brace) return row;
                return fail(pos_, "unbalanced '}'");
            }
            if (stop == Stop::bracket && c == ']') return row;
            if (stop == Stop::right && at_command("right")) return row;
            if (stop == Stop::env && (c == '&' || s_.compare(pos_, 2, "\\\\") == 0 || at_command("end"))) return row;
            if (c == '&') return fail(pos_, "'&' outside an environment");
            if (s_.compare(pos_, 2, "\\\\") == 0) {
                pos_ += 2;
                MathNode n;
                n.kind = MathNode::Kind::space;
                n.text = n.ascii = " ";
                row.children.push_back(std::move(n));
                continue;
            }
            std::expected<MathNode, LatexError> base = MathNode{};
            if (c == '_' || c == '^') base = MathNode{};
            else base = atom();
            if (!base) return base;
            if (base->kind == MathNode::Kind::space && base->text.empty() && c == '\\') {
                row.children.push_back(std::move(*base));
                continue;
            }
            std::optional<MathNode> sub, sup;
            while (true) {
                skip_spaces();
                if (pos_ >= s_.size() || (s_[pos_] != '_' && s_[pos_] != '^')) break;
                const bool is_sub = s_[pos_] == '_';
                const auto at = pos_++;
                if ((is_sub && sub) || (!is_sub && sup)) return fail(at, "double script");
                auto arg = argument();
                if (!arg) return fail(at, "missing script after '" + std::string(1, is_sub ? '_' : '^') + "'");
                (is_sub ? sub : sup) = std::move(*arg);
            }
            if (!sub && !sup) {
                row.children.push_back(std::move(*base));
                continue;
            }
            MathNode s;
            s.kind = MathNode::Kind::scripts;
            s.has_sub = sub.has_value();
            s.has_sup = sup.has_value();
            s.children.push_back(std::move(*base));
            s.children.push_back(sub ? std::move(*sub) : MathNode{});
            s.children.push_back(sup ? std::move(*sup) : MathNode{});
            row.children.push_back(std::move(s));
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

}  // namespace

std::expected<MathNode, LatexError> parse_latex(std::string_view src) { return Parser{src}.run(); }

std::vector<std::string_view> supported_commands() {
    std::vector<std::string_view> out;
    for (const auto& g : kSymbols) out.push_back(g.command);
    for (const auto& g : kBigOps) out.push_back(g.command);
    for (auto f : kFunctions) out.push_back(f);
    for (const auto& s : kSpaces) out.push_back(s.command);
    for (auto a : kAccents) out.push_back(a);
    for (auto f : kFonts) out.push_back(f);
    for (std::string_view c : {"bmod", "frac", "tfrac", "dfrac", "binom", "sqrt", "mathbb", "mathcal", "left", "right", "big", "Big", "begin", "end"})
        out.push_back(c);
    return out;
}

}  // namespace signals::theory
