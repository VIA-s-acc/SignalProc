#include "theory/math_layout.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <utility>

#include "theory/text.hpp"

namespace signals::theory {
namespace {

using Kind = MathNode::Kind;

struct Ctx {
    MathGlyphs glyphs = MathGlyphs::unicode;
    bool tight = false;
};

enum class Cls { ord, bin, rel, punct, open, close, op, space };

constexpr std::array<std::u32string_view, 21> kRelations{U"=", U"≈", U"≤", U"≥", U"≠", U"∼", U"∝", U"≡", U"∈", U"∉", U"⊂",
                                                         U"⊆", U"→", U"⇔", U"⇒", U"←", U"↔", U"<", U">", U"∣", U":="};
constexpr std::array<std::u32string_view, 11> kBinary{U"+", U"−", U"±", U"×", U"·", U"⊗", U"⊕", U"⊛", U"∪", U"∩", U"∗"};

struct Pair {
    char32_t from, to;
};

constexpr std::array kSuperscripts{
    Pair{U'0', U'⁰'}, Pair{U'1', U'¹'}, Pair{U'2', U'²'}, Pair{U'3', U'³'}, Pair{U'4', U'⁴'}, Pair{U'5', U'⁵'}, Pair{U'6', U'⁶'},
    Pair{U'7', U'⁷'}, Pair{U'8', U'⁸'}, Pair{U'9', U'⁹'}, Pair{U'+', U'⁺'}, Pair{U'−', U'⁻'}, Pair{U'-', U'⁻'}, Pair{U'=', U'⁼'},
    Pair{U'(', U'⁽'}, Pair{U')', U'⁾'}, Pair{U'a', U'ᵃ'}, Pair{U'b', U'ᵇ'}, Pair{U'c', U'ᶜ'}, Pair{U'd', U'ᵈ'}, Pair{U'e', U'ᵉ'},
    Pair{U'f', U'ᶠ'}, Pair{U'g', U'ᵍ'}, Pair{U'h', U'ʰ'}, Pair{U'i', U'ⁱ'}, Pair{U'j', U'ʲ'}, Pair{U'k', U'ᵏ'}, Pair{U'l', U'ˡ'},
    Pair{U'm', U'ᵐ'}, Pair{U'n', U'ⁿ'}, Pair{U'o', U'ᵒ'}, Pair{U'p', U'ᵖ'}, Pair{U'r', U'ʳ'}, Pair{U's', U'ˢ'}, Pair{U't', U'ᵗ'},
    Pair{U'u', U'ᵘ'}, Pair{U'v', U'ᵛ'}, Pair{U'w', U'ʷ'}, Pair{U'x', U'ˣ'}, Pair{U'y', U'ʸ'}, Pair{U'z', U'ᶻ'}, Pair{U'A', U'ᴬ'},
    Pair{U'B', U'ᴮ'}, Pair{U'D', U'ᴰ'}, Pair{U'E', U'ᴱ'}, Pair{U'G', U'ᴳ'}, Pair{U'H', U'ᴴ'}, Pair{U'I', U'ᴵ'}, Pair{U'J', U'ᴶ'},
    Pair{U'K', U'ᴷ'}, Pair{U'L', U'ᴸ'}, Pair{U'M', U'ᴹ'}, Pair{U'N', U'ᴺ'}, Pair{U'O', U'ᴼ'}, Pair{U'P', U'ᴾ'}, Pair{U'R', U'ᴿ'},
    Pair{U'T', U'ᵀ'}, Pair{U'U', U'ᵁ'}, Pair{U'V', U'ⱽ'}, Pair{U'W', U'ᵂ'}, Pair{U'θ', U'ᶿ'}, Pair{U'′', U'′'},
};

constexpr std::array kSubscripts{
    Pair{U'0', U'₀'}, Pair{U'1', U'₁'}, Pair{U'2', U'₂'}, Pair{U'3', U'₃'}, Pair{U'4', U'₄'}, Pair{U'5', U'₅'}, Pair{U'6', U'₆'},
    Pair{U'7', U'₇'}, Pair{U'8', U'₈'}, Pair{U'9', U'₉'}, Pair{U'+', U'₊'}, Pair{U'−', U'₋'}, Pair{U'-', U'₋'}, Pair{U'=', U'₌'},
    Pair{U'(', U'₍'}, Pair{U')', U'₎'}, Pair{U'a', U'ₐ'}, Pair{U'e', U'ₑ'}, Pair{U'o', U'ₒ'}, Pair{U'x', U'ₓ'},
    Pair{U'i', U'ᵢ'}, Pair{U'j', U'ⱼ'}, Pair{U'r', U'ᵣ'}, Pair{U'u', U'ᵤ'}, Pair{U'v', U'ᵥ'},
};

struct Vulgar {
    std::string_view num, den, glyph;
};
constexpr std::array kVulgar{Vulgar{"1", "2", "½"},  Vulgar{"1", "3", "⅓"}, Vulgar{"2", "3", "⅔"}, Vulgar{"1", "4", "¼"}, Vulgar{"3", "4", "¾"},
                             Vulgar{"1", "5", "⅕"},  Vulgar{"2", "5", "⅖"}, Vulgar{"3", "5", "⅗"}, Vulgar{"4", "5", "⅘"}, Vulgar{"1", "6", "⅙"},
                             Vulgar{"5", "6", "⅚"},  Vulgar{"1", "8", "⅛"}, Vulgar{"3", "8", "⅜"}, Vulgar{"5", "8", "⅝"}, Vulgar{"7", "8", "⅞"},
                             Vulgar{"1", "7", "⅐"},  Vulgar{"1", "9", "⅑"}, Vulgar{"1", "10", "⅒"}};

struct Delim {
    std::string_view glyph, ascii;
};
constexpr std::array kDelimAscii{Delim{"⟨", "<"}, Delim{"⟩", ">"}, Delim{"‖", "||"}, Delim{"⌊", "|_"}, Delim{"⌋", "_|"}, Delim{"⌈", "|^"},
                                 Delim{"⌉", "^|"}, Delim{"∣", "|"}, Delim{"│", "|"}};

int width_of(const std::string& s) { return static_cast<int>(display_width(s)); }
std::string spaces(int n) { return std::string(static_cast<std::size_t>(std::max(0, n)), ' '); }

std::string delim_text(const std::string& d, MathGlyphs g) {
    if (g == MathGlyphs::unicode) return d;
    for (const auto& x : kDelimAscii)
        if (x.glyph == d) return std::string{x.ascii};
    return d;
}

std::string glyph_of(const MathNode& n, MathGlyphs g) { return g == MathGlyphs::ascii && !n.ascii.empty() ? n.ascii : n.text; }

Cls cls_of(const MathNode& n) {
    switch (n.kind) {
    case Kind::symbol: {
        const auto t = to_u32(n.text);
        if (t == U"," || t == U";") return Cls::punct;
        if (t == U"(" || t == U"[" || t == U"⟨" || t == U"⌊" || t == U"⌈" || t == U"{") return Cls::open;
        if (t == U")" || t == U"]" || t == U"⟩" || t == U"⌋" || t == U"⌉" || t == U"}") return Cls::close;
        for (auto r : kRelations)
            if (t == r) return Cls::rel;
        for (auto b : kBinary)
            if (t == b) return Cls::bin;
        return Cls::ord;
    }
    case Kind::function:
    case Kind::big_op: return Cls::op;
    case Kind::scripts: {
        const auto& base = n.children[0];
        if (base.kind == Kind::function || base.kind == Kind::big_op) return Cls::op;
        if (base.kind == Kind::symbol) {
            const auto c = cls_of(base);
            if (c == Cls::close || c == Cls::rel) return c;
        }
        return Cls::ord;
    }
    case Kind::space: return Cls::space;
    default: break;
    }
    return Cls::ord;
}

std::vector<Cls> classes(const std::vector<MathNode>& items) {
    std::vector<Cls> out;
    for (const auto& n : items) {
        auto c = cls_of(n);
        if (c == Cls::bin) {
            Cls prev = Cls::open;
            for (auto it = out.rbegin(); it != out.rend(); ++it)
                if (*it != Cls::space) {
                    prev = *it;
                    break;
                }
            if (prev == Cls::bin || prev == Cls::rel || prev == Cls::open || prev == Cls::punct || prev == Cls::op) c = Cls::ord;
        }
        out.push_back(c);
    }
    return out;
}

bool next_takes_space(Cls c) { return c == Cls::ord; }

template <class Emit>
void spaced_row(const std::vector<MathNode>& items, bool tight, Emit&& emit) {
    const auto cls = classes(items);
    bool any = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto c = cls[i];
        if (items[i].kind == Kind::space && items[i].text.empty()) continue;
        const bool last = i + 1 == items.size();
        if (!tight && any && (c == Cls::bin || c == Cls::rel)) emit(nullptr, " ");
        else if (!tight && any && items[i].kind == Kind::matrix && !items[i].left.empty() && cls[i - 1] == Cls::ord) emit(nullptr, " ");
        emit(&items[i], {});
        any = true;
        if (tight || last) continue;
        if (c == Cls::bin || c == Cls::rel || c == Cls::punct) emit(nullptr, " ");
        else if (c == Cls::op && next_takes_space(cls[i + 1])) emit(nullptr, " ");
    }
}

std::optional<std::string> convert(const std::string& text, bool sup) {
    std::u32string out;
    for (char32_t c : to_u32(text)) {
        bool found = false;
        const auto& table = sup ? std::span<const Pair>{kSuperscripts} : std::span<const Pair>{kSubscripts};
        for (const auto& p : table)
            if (p.from == c) {
                out.push_back(p.to);
                found = true;
                break;
            }
        if (!found) return std::nullopt;
    }
    return to_utf8(out);
}

bool visible(const MathNode& n) { return !(n.kind == Kind::space && n.text.empty()); }

std::size_t visible_count(const MathNode& n) {
    std::size_t k = 0;
    for (const auto& c : n.children) k += visible(c) ? 1 : 0;
    return k;
}

bool is_digits(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

std::string linear(const MathNode& n, Ctx ctx);

bool atomic(const MathNode& n, Ctx ctx) {
    if (n.kind == Kind::symbol || n.kind == Kind::text || n.kind == Kind::fenced || n.kind == Kind::function) return true;
    if (n.kind == Kind::scripts) return atomic(n.children[0], ctx);
    if (n.kind == Kind::row) {
        if (visible_count(n) <= 1) return n.children.empty() || atomic(n.children.front(), ctx);
        for (const auto& c : n.children)
            if (c.kind != Kind::symbol || cls_of(c) != Cls::ord) return false;
        return display_width(linear(n, ctx)) <= 3;
    }
    if (n.kind == Kind::frac) return n.text == "tfrac" || n.text == "frac";
    return false;
}

std::string wrap(const MathNode& n, Ctx ctx) {
    const auto s = linear(n, ctx);
    if (n.kind == Kind::row && visible_count(n) > 1) return "(" + s + ")";
    if (n.kind == Kind::frac) return "(" + s + ")";
    return s;
}

std::optional<std::string> vulgar(const MathNode& frac, Ctx ctx) {
    if (ctx.glyphs != MathGlyphs::unicode) return std::nullopt;
    const auto a = linear(frac.children[0], ctx), b = linear(frac.children[1], ctx);
    if (!is_digits(a) || !is_digits(b)) return std::nullopt;
    for (const auto& v : kVulgar)
        if (v.num == a && v.den == b) return std::string{v.glyph};
    return std::nullopt;
}

std::string accent_mark(const std::string& name) {
    if (name == "hat") return "̂";
    if (name == "tilde") return "̃";
    if (name == "bar") return "̄";
    return "̅";
}

std::string with_combining(const std::string& s, const std::string& mark) {
    std::u32string out;
    const auto m = to_u32(mark).front();
    for (char32_t c : to_u32(s)) {
        out.push_back(c);
        if (c != U' ' && !is_combining(c)) out.push_back(m);
    }
    return to_utf8(out);
}

std::string script_text(const MathNode& s, bool sup, Ctx ctx) {
    Ctx tight{ctx.glyphs, true};
    const auto text = linear(s, tight);
    if (ctx.glyphs == MathGlyphs::unicode)
        if (auto c = convert(text, sup)) return *c;
    const std::string mark = sup ? "^" : "_";
    return mark + (display_width(text) > 1 ? "(" + text + ")" : text);
}

std::string linear(const MathNode& n, Ctx ctx) {
    switch (n.kind) {
    case Kind::row: {
        std::string out;
        spaced_row(n.children, ctx.tight, [&](const MathNode* item, std::string_view gap) { out += item ? linear(*item, ctx) : std::string{gap}; });
        return out;
    }
    case Kind::symbol: return glyph_of(n, ctx.glyphs);
    case Kind::text: return glyph_of(n, ctx.glyphs);
    case Kind::space: return n.text;
    case Kind::function: return n.text;
    case Kind::big_op: return glyph_of(n, ctx.glyphs);
    case Kind::frac: {
        if (n.text == "binom") return "C(" + linear(n.children[0], ctx) + ", " + linear(n.children[1], ctx) + ")";
        if (auto v = vulgar(n, ctx)) return *v;
        return wrap(n.children[0], ctx) + "/" + wrap(n.children[1], ctx);
    }
    case Kind::sqrt: {
        const auto r = linear(n.children[0], ctx);
        if (ctx.glyphs == MathGlyphs::ascii)
            return n.children.size() > 1 ? "root(" + linear(n.children[1], ctx) + ", " + r + ")" : "sqrt(" + r + ")";
        std::string sign = "√";
        if (n.children.size() > 1) {
            const auto deg = linear(n.children[1], ctx);
            sign = deg == "3" ? "∛" : deg == "4" ? "∜" : convert(deg, true).value_or("(" + deg + ")") + "√";
        }
        return sign + (atomic(n.children[0], ctx) ? r : "(" + r + ")");
    }
    case Kind::scripts: {
        std::string out = linear(n.children[0], ctx);
        if (n.has_sub) out += script_text(n.children[1], false, ctx);
        if (n.has_sup) out += script_text(n.children[2], true, ctx);
        return out;
    }
    case Kind::fenced: return delim_text(n.left, ctx.glyphs) + linear(n.children[0], ctx) + delim_text(n.right, ctx.glyphs);
    case Kind::matrix: {
        std::string out = delim_text(n.left, ctx.glyphs);
        for (std::size_t i = 0; i < n.children.size(); ++i) {
            if (i > 0) out += i % n.columns == 0 ? "; " : " ";
            out += linear(n.children[i], ctx);
        }
        return out + delim_text(n.right, ctx.glyphs);
    }
    case Kind::accent: {
        const auto a = linear(n.children[0], ctx);
        if (ctx.glyphs == MathGlyphs::ascii) return (n.text == "overline" ? std::string{"bar"} : n.text) + "(" + a + ")";
        return with_combining(a, accent_mark(n.text));
    }
    }
    return {};
}

MathBox text_box(std::string s) {
    MathBox b;
    b.width = width_of(s);
    b.lines = {std::move(s)};
    return b;
}

MathBox hcat(const std::vector<MathBox>& parts) {
    int up = 0, down = 0;
    for (const auto& p : parts) {
        up = std::max(up, p.baseline);
        down = std::max(down, static_cast<int>(p.lines.size()) - p.baseline - 1);
    }
    MathBox out;
    out.baseline = up;
    out.lines.assign(static_cast<std::size_t>(up + down + 1), "");
    for (const auto& p : parts) {
        const int top = up - p.baseline;
        for (int r = 0; r < static_cast<int>(out.lines.size()); ++r) {
            const int pr = r - top;
            auto& line = out.lines[static_cast<std::size_t>(r)];
            if (pr >= 0 && pr < static_cast<int>(p.lines.size())) {
                const auto& src = p.lines[static_cast<std::size_t>(pr)];
                line += src + spaces(p.width - width_of(src));
            } else {
                line += spaces(p.width);
            }
        }
        out.width += p.width;
    }
    return out;
}

std::string centred(const std::string& s, int w) {
    const int pad = w - width_of(s);
    return spaces(pad / 2) + s + spaces(pad - pad / 2);
}

MathBox stack(const std::vector<MathBox>& parts, int baseline_part, int baseline_offset_in_part) {
    MathBox out;
    for (const auto& p : parts) out.width = std::max(out.width, p.width);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (static_cast<int>(i) == baseline_part) out.baseline = static_cast<int>(out.lines.size()) + baseline_offset_in_part;
        for (const auto& l : parts[i].lines) out.lines.push_back(centred(l + spaces(parts[i].width - width_of(l)), out.width));
    }
    return out;
}

MathBox grow(const std::string& d, int height, int baseline, MathGlyphs g) {
    MathBox b;
    b.baseline = baseline;
    const bool ascii = g == MathGlyphs::ascii;
    auto column = [&](std::string top, std::string mid, std::string bot, std::string centre = {}) {
        for (int r = 0; r < height; ++r) {
            if (r == 0) b.lines.push_back(top);
            else if (r == height - 1) b.lines.push_back(bot);
            else if (!centre.empty() && r == (height - 1) / 2) b.lines.push_back(centre);
            else b.lines.push_back(mid);
        }
    };
    if (d.empty()) {
        b.lines.assign(static_cast<std::size_t>(height), "");
        return b;
    }
    if (height <= 1) {
        b = text_box(delim_text(d, g));
        b.baseline = 0;
        return b;
    }
    if (d == "(") column(ascii ? "/" : "⎛", ascii ? "|" : "⎜", ascii ? "\\" : "⎝");
    else if (d == ")") column(ascii ? "\\" : "⎞", ascii ? "|" : "⎟", ascii ? "/" : "⎠");
    else if (d == "[") column(ascii ? "[" : "⎡", ascii ? "[" : "⎢", ascii ? "[" : "⎣");
    else if (d == "]") column(ascii ? "]" : "⎤", ascii ? "]" : "⎥", ascii ? "]" : "⎦");
    else if (d == "{") column(ascii ? "/" : "⎧", ascii ? "|" : "⎪", ascii ? "\\" : "⎩", height > 2 ? (ascii ? "<" : "⎨") : "");
    else if (d == "}") column(ascii ? "\\" : "⎫", ascii ? "|" : "⎪", ascii ? "/" : "⎭", height > 2 ? (ascii ? ">" : "⎬") : "");
    else if (d == "|" || d == "∣") column(ascii ? "|" : "│", ascii ? "|" : "│", ascii ? "|" : "│");
    else if (d == "‖") column(ascii ? "||" : "║", ascii ? "||" : "║", ascii ? "||" : "║");
    else if (d == "⌊") column(ascii ? "|" : "│", ascii ? "|" : "│", ascii ? "|_" : "└");
    else if (d == "⌋") column(ascii ? "|" : "│", ascii ? "|" : "│", ascii ? "_|" : "┘");
    else if (d == "⌈") column(ascii ? "|^" : "┌", ascii ? "|" : "│", ascii ? "|" : "│");
    else if (d == "⌉") column(ascii ? "^|" : "┐", ascii ? "|" : "│", ascii ? "|" : "│");
    else {
        const auto t = delim_text(d, g);
        for (int r = 0; r < height; ++r) b.lines.push_back(r == baseline ? t : spaces(width_of(t)));
    }
    for (const auto& l : b.lines) b.width = std::max(b.width, width_of(l));
    for (auto& l : b.lines) l += spaces(b.width - width_of(l));
    return b;
}

MathBox display(const MathNode& n, Ctx ctx);

MathBox frac_box(const MathBox& num, const MathBox& den, MathGlyphs g, bool bar) {
    const int w = std::max(num.width, den.width) + (bar ? 2 : 0);
    MathBox out;
    out.width = w;
    for (const auto& l : num.lines) out.lines.push_back(centred(l + spaces(num.width - width_of(l)), w));
    out.baseline = static_cast<int>(out.lines.size()) - (bar ? 0 : 1);
    if (bar) {
        std::string line;
        for (int i = 0; i < w; ++i) line += g == MathGlyphs::ascii ? "-" : "─";
        out.lines.push_back(line);
    }
    for (const auto& l : den.lines) out.lines.push_back(centred(l + spaces(den.width - width_of(l)), w));
    return out;
}

MathBox radical(const MathBox& r, MathGlyphs g) {
    const int h = static_cast<int>(r.lines.size());
    const bool ascii = g == MathGlyphs::ascii;
    MathBox out;
    out.width = h + 1 + r.width;
    out.baseline = r.baseline + 1;
    out.lines.push_back(spaces(h + 1) + std::string(static_cast<std::size_t>(r.width), '_'));
    for (int k = 1; k <= h; ++k) {
        std::string left = spaces(h + 1);
        if (k == h) left = std::string{ascii ? "\\/" : "╲╱"} + spaces(h - 1);
        else left = spaces(h - k) + (ascii ? "/" : "╱") + spaces(k);
        const auto& src = r.lines[static_cast<std::size_t>(k - 1)];
        out.lines.push_back(left + src + spaces(r.width - width_of(src)));
    }
    return out;
}

MathBox scripts_box(const MathNode& n, Ctx ctx) {
    const auto& base = n.children[0];
    Ctx tight{ctx.glyphs, true};
    if ((base.kind == Kind::big_op || base.kind == Kind::function) && base.limits) {
        std::vector<MathBox> parts;
        int base_index = 0;
        if (n.has_sup) {
            parts.push_back(display(n.children[2], tight));
            base_index = 1;
        }
        parts.push_back(display(base, ctx));
        if (n.has_sub) parts.push_back(display(n.children[1], tight));
        return stack(parts, base_index, 0);
    }
    auto b = display(base, ctx);
    if (ctx.glyphs == MathGlyphs::unicode) {
        const auto sub = n.has_sub ? convert(linear(n.children[1], tight), false) : std::optional<std::string>{""};
        const auto sup = n.has_sup ? convert(linear(n.children[2], tight), true) : std::optional<std::string>{""};
        if (sub && sup) {
            if (b.lines.size() == 1) return text_box(b.lines[0] + spaces(b.width - width_of(b.lines[0])) + *sub + *sup);
            MathBox column;
            column.width = std::max(width_of(*sub), width_of(*sup));
            column.baseline = b.baseline;
            for (std::size_t r = 0; r < b.lines.size(); ++r) {
                std::string cell = r == 0 ? *sup : r + 1 == b.lines.size() ? *sub : std::string{};
                column.lines.push_back(cell + spaces(column.width - width_of(cell)));
            }
            return hcat({b, column});
        }
    } else if (b.lines.size() == 1) {
        return text_box(linear(n, ctx));
    }
    MathBox column;
    MathBox sup = n.has_sup ? display(n.children[2], tight) : MathBox{};
    MathBox sub = n.has_sub ? display(n.children[1], tight) : MathBox{};
    column.width = std::max(sup.width, sub.width);
    for (const auto& l : sup.lines) column.lines.push_back(l + spaces(column.width - width_of(l)));
    column.baseline = static_cast<int>(column.lines.size());
    column.lines.push_back(spaces(column.width));
    for (const auto& l : sub.lines) column.lines.push_back(l + spaces(column.width - width_of(l)));
    return hcat({b, column});
}

MathBox matrix_box(const MathNode& n, Ctx ctx) {
    const std::size_t cols = std::max<std::size_t>(1, n.columns);
    const std::size_t rows = (n.children.size() + cols - 1) / cols;
    std::vector<MathBox> cells;
    for (const auto& c : n.children) cells.push_back(display(c, ctx));
    std::vector<int> widths(cols, 0);
    for (std::size_t i = 0; i < cells.size(); ++i) widths[i % cols] = std::max(widths[i % cols], cells[i].width);
    const bool left_align = n.text == "cases" || n.text == "aligned";
    std::vector<MathBox> row_boxes;
    for (std::size_t r = 0; r < rows; ++r) {
        std::vector<MathBox> parts;
        for (std::size_t c = 0; c < cols; ++c) {
            auto cell = r * cols + c < cells.size() ? cells[r * cols + c] : text_box("");
            const int pad = widths[c] - cell.width;
            const int left = left_align ? 0 : pad / 2;
            for (auto& l : cell.lines) l = spaces(left) + l + spaces(cell.width - width_of(l)) + spaces(pad - left);
            cell.width = widths[c];
            if (c > 0) parts.push_back(text_box("  "));
            parts.push_back(std::move(cell));
        }
        row_boxes.push_back(hcat(parts));
    }
    MathBox body;
    for (const auto& rb : row_boxes) {
        body.width = std::max(body.width, rb.width);
        for (const auto& l : rb.lines) body.lines.push_back(l);
    }
    for (auto& l : body.lines) l += spaces(body.width - width_of(l));
    if (body.lines.empty()) body.lines.push_back("");
    body.baseline = static_cast<int>(body.lines.size() - 1) / 2;
    const int h = static_cast<int>(body.lines.size());
    std::vector<MathBox> parts;
    if (!n.left.empty()) parts.push_back(grow(n.left, h, body.baseline, ctx.glyphs));
    if (!n.left.empty()) parts.push_back(text_box(" "));
    parts.push_back(body);
    if (!n.right.empty()) parts.push_back(text_box(" "));
    if (!n.right.empty()) parts.push_back(grow(n.right, h, body.baseline, ctx.glyphs));
    return hcat(parts);
}

MathBox display(const MathNode& n, Ctx ctx) {
    switch (n.kind) {
    case Kind::row: {
        std::vector<MathBox> parts;
        spaced_row(n.children, ctx.tight, [&](const MathNode* item, std::string_view gap) {
            parts.push_back(item ? display(*item, ctx) : text_box(std::string{gap}));
        });
        if (parts.empty()) return text_box("");
        return hcat(parts);
    }
    case Kind::frac: {
        if (n.text == "tfrac") return text_box(linear(n, ctx));
        auto num = display(n.children[0], ctx), den = display(n.children[1], ctx);
        return frac_box(num, den, ctx.glyphs, n.text != "binom");
    }
    case Kind::sqrt: {
        auto r = display(n.children[0], ctx);
        if (r.lines.size() == 1) return text_box(linear(n, ctx));
        return radical(r, ctx.glyphs);
    }
    case Kind::scripts: return scripts_box(n, ctx);
    case Kind::fenced: {
        auto inner = display(n.children[0], ctx);
        if (inner.lines.size() == 1) return text_box(linear(n, ctx));
        const int h = static_cast<int>(inner.lines.size());
        return hcat({grow(n.left, h, inner.baseline, ctx.glyphs), inner, grow(n.right, h, inner.baseline, ctx.glyphs)});
    }
    case Kind::matrix: return matrix_box(n, ctx);
    case Kind::accent: {
        auto a = display(n.children[0], ctx);
        if (a.lines.size() == 1 && a.width == 1 && ctx.glyphs == MathGlyphs::unicode) return text_box(linear(n, ctx));
        MathBox out = a;
        std::string mark;
        if (n.text == "hat") mark = centred("^", a.width);
        else if (n.text == "tilde") mark = centred("~", a.width);
        else
            for (int i = 0; i < a.width; ++i) mark += ctx.glyphs == MathGlyphs::ascii ? "_" : "‾";
        out.lines.insert(out.lines.begin(), mark);
        out.baseline += 1;
        return out;
    }
    default: return text_box(linear(n, ctx));
    }
}

}  // namespace

MathBox layout(const MathNode& node, MathStyle style, MathGlyphs glyphs) {
    const Ctx ctx{glyphs, false};
    if (style == MathStyle::inline_linear) return text_box(linear(node, ctx));
    auto b = display(node, ctx);
    for (auto& l : b.lines) l += spaces(b.width - width_of(l));
    return b;
}

std::expected<MathBox, LatexError> render_latex(std::string_view src, MathStyle style, MathGlyphs glyphs) {
    auto node = parse_latex(src);
    if (!node) return std::unexpected{node.error()};
    return layout(*node, style, glyphs);
}

}  // namespace signals::theory
