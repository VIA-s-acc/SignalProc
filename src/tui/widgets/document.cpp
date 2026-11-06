#include "tui/widgets/document.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "theory/text.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/screen_util.hpp"

namespace signals::tui {
namespace {

using theory::Block;
using theory::Inline;

enum class Style { normal, dim, strong, emphasis, code, link, heading, keyword, math, ok, err };

struct Span {
    std::string text;
    Style style = Style::normal;
    int link = -1;
};

struct Line {
    std::vector<Span> spans;
    bool code = false;
    int formula = -1;
};

struct Token {
    std::string text;
    Style style = Style::normal;
    int link = -1;
    bool space_before = false;
};

constexpr std::array<std::string_view, 12> kKeywords{"for", "if", "else", "while", "return", "const", "auto", "double", "std", "static_cast", "bool", "int"};

int width_of(const std::string& s) { return static_cast<int>(theory::display_width(s)); }

std::string repeat(std::string_view s, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

class Builder {
public:
    explicit Builder(const DocumentSpec& spec) : spec_{spec}, values_{spec.values ? spec.values : &empty_} {}

    void run() {
        Block::Kind previous = Block::Kind::rule;
        bool first = true;
        for (const auto& b : spec_.blocks) {
            const bool list = b.kind == Block::Kind::bullet || b.kind == Block::Kind::numbered;
            const bool prev_list = previous == Block::Kind::bullet || previous == Block::Kind::numbered;
            if (!first && !(list && prev_list)) lines_.push_back({});
            first = false;
            previous = b.kind;
            block(b);
        }
    }

    std::vector<Line> lines_;
    std::vector<DocLink> links_;
    std::vector<DocFormula> formulas_;

private:
    bool ascii() const { return spec_.glyphs == theory::MathGlyphs::ascii; }

    std::string number(double v) const { return format_number(v, 2); }

    std::string prose(std::string_view text) const {
        return theory::substitute(text, *values_, theory::Context::prose, [this](double v) { return number(v); });
    }
    std::string math_source(std::string_view text) const {
        return theory::substitute(text, *values_, theory::Context::math, [this](double v) { return number(v); });
    }

    void block(const Block& b) {
        switch (b.kind) {
        case Block::Kind::heading: heading(b); break;
        case Block::Kind::paragraph: wrap(tokens(b.inlines), "", ""); break;
        case Block::Kind::bullet: {
            const std::string indent(static_cast<std::size_t>(2 * b.level), ' ');
            wrap(tokens(b.inlines), indent + (ascii() ? "- " : "• "), indent + "  ");
            break;
        }
        case Block::Kind::numbered: {
            const std::string indent(static_cast<std::size_t>(2 * b.level), ' ');
            const auto marker = std::to_string(b.number) + ". ";
            wrap(tokens(b.inlines), indent + marker, indent + std::string(marker.size(), ' '));
            break;
        }
        case Block::Kind::display_math: display_math(b.text); break;
        case Block::Kind::table: table(b); break;
        case Block::Kind::code: code(b); break;
        case Block::Kind::rule: lines_.push_back({{{repeat(ascii() ? "-" : "─", spec_.width), Style::dim}}}); break;
        }
    }

    void heading(const Block& b) {
        std::string text;
        for (const auto& t : tokens(b.inlines)) text += (t.space_before && !text.empty() ? " " : "") + t.text;
        if (!spec_.section_rule.empty()) {
            const std::string h = ascii() ? "-" : "─";
            std::string head = h + h + " " + text + " ";
            if (width_of(head) > spec_.width) head = cut_columns(head, 0, spec_.width);
            lines_.push_back({{{head + repeat(h, spec_.width - width_of(head)), Style::dim}}});
            return;
        }
        if (width_of(text) > spec_.width) text = cut_columns(text, 0, spec_.width - 1) + (ascii() ? "~" : "…");
        lines_.push_back({{{text, Style::heading}}});
    }

    void words(std::string_view text, Style style, std::vector<Token>& out, bool& pending) {
        std::string word;
        auto flush = [&] {
            if (word.empty()) return;
            out.push_back({word, style, -1, pending});
            pending = false;
            word.clear();
        };
        for (char c : text) {
            if (c == ' ' || c == '\n' || c == '\t') {
                flush();
                pending = true;
            } else {
                word += c;
            }
        }
        flush();
    }

    void collect(const std::vector<Inline>& in, Style base, std::vector<Token>& out, bool& pending) {
        for (const auto& i : in) {
            switch (i.kind) {
            case Inline::Kind::text: words(i.text, base, out, pending); break;
            case Inline::Kind::strong: collect(i.children, Style::strong, out, pending); break;
            case Inline::Kind::emphasis: collect(i.children, Style::emphasis, out, pending); break;
            case Inline::Kind::code:
                out.push_back({i.text, Style::code, -1, pending});
                pending = false;
                break;
            case Inline::Kind::math: {
                auto box = theory::render_latex(math_source(i.text), theory::MathStyle::inline_linear, spec_.glyphs);
                if (box && !box->lines.empty()) out.push_back({box->lines[0], Style::math, -1, pending});
                else out.push_back({"$" + i.text + "$", Style::dim, -1, pending});
                pending = false;
                break;
            }
            case Inline::Kind::ref:
                links_.push_back({"#" + i.target, i.text, {}});
                out.push_back({i.text, Style::link, static_cast<int>(links_.size()) - 1, pending});
                pending = false;
                break;
            case Inline::Kind::placeholder: {
                const auto value = prose("{{" + i.text + "}}");
                const auto style = value.starts_with("✓") ? Style::ok : value.starts_with("✗") ? Style::err : value.starts_with("—") ? Style::dim : base;
                out.push_back({value, style, -1, pending});
                pending = false;
                break;
            }
            }
        }
    }

    std::vector<Token> tokens(const std::vector<Inline>& in) {
        std::vector<Token> out;
        bool pending = false;
        collect(in, Style::normal, out, pending);
        return out;
    }

    void wrap(const std::vector<Token>& tokens, const std::string& first, const std::string& rest) {
        Line line;
        int used = 0;
        auto start = [&](const std::string& prefix) {
            line = {};
            if (!prefix.empty()) line.spans.push_back({prefix, Style::dim});
            used = width_of(prefix);
        };
        start(first);
        bool empty = true;
        const int rest_width = width_of(rest);
        for (const auto& t : tokens) {
            int w = width_of(t.text);
            std::string text = t.text;
            const int room = spec_.width - rest_width;
            if (w > room) {
                text = cut_columns(text, 0, std::max(1, room - 1)) + (ascii() ? "~" : "…");
                w = width_of(text);
            }
            const int gap = !empty && t.space_before ? 1 : 0;
            if (!empty && used + gap + w > spec_.width) {
                lines_.push_back(std::move(line));
                start(rest);
                empty = true;
            }
            if (!empty && t.space_before) {
                line.spans.push_back({" ", Style::normal});
                ++used;
            }
            if (t.link >= 0 && links_[static_cast<std::size_t>(t.link)].line < 0)
                links_[static_cast<std::size_t>(t.link)].line = static_cast<int>(lines_.size());
            line.spans.push_back({text, t.style, t.link});
            used += w;
            empty = false;
        }
        lines_.push_back(std::move(line));
    }

    void display_math(const std::string& src) {
        auto box = theory::render_latex(math_source(src), theory::MathStyle::display, spec_.glyphs);
        formulas_.push_back({src, {}});
        const int index = static_cast<int>(formulas_.size()) - 1;
        if (!box) {
            lines_.push_back({{{"  " + cut_columns(src, 0, spec_.width - 2), Style::dim}}, false, index});
            const auto note = fmt::format(fmt::runtime(tr("tui.doc.bad_formula")), fmt::arg("message", box.error().message));
            lines_.push_back({{{cut_columns("  " + note, 0, spec_.width), Style::err}}, false, index});
            return;
        }
        const int room = spec_.width - 2;
        const bool wide = box->width > room;
        const int from = wide ? std::clamp(spec_.hscroll, 0, std::max(0, box->width - room + 1)) : 0;
        for (const auto& l : box->lines) {
            std::string shown = l;
            if (wide) {
                const bool more = from + room - 1 < box->width;
                shown = cut_columns(l, from, more ? room - 1 : room);
                if (more) shown += ascii() ? "~" : "…";
            }
            lines_.push_back({{{"  " + shown, Style::math}}, false, index});
        }
    }

    std::string plain(const std::vector<Inline>& in) {
        std::string out;
        for (const auto& t : tokens(in)) out += (t.space_before && !out.empty() ? " " : "") + t.text;
        return out;
    }

    void table(const Block& b) {
        if (b.table.empty()) return;
        std::vector<std::vector<std::string>> cells;
        std::size_t cols = 0;
        for (const auto& row : b.table) {
            std::vector<std::string> r;
            for (const auto& cell : row) r.push_back(plain(cell));
            cols = std::max(cols, r.size());
            cells.push_back(std::move(r));
        }
        std::vector<int> widths(cols, 0);
        for (auto& r : cells) {
            r.resize(cols);
            for (std::size_t c = 0; c < cols; ++c) widths[c] = std::max(widths[c], width_of(r[c]));
        }
        const std::string sep = ascii() ? " | " : " │ ";
        auto total = [&] {
            int t = 3 * static_cast<int>(cols - 1);
            for (int w : widths) t += w;
            return t;
        };
        while (total() > spec_.width) {
            auto widest = std::max_element(widths.begin(), widths.end());
            if (*widest <= 3) break;
            --*widest;
        }
        const std::string ellipsis = ascii() ? "~" : "…";
        for (std::size_t r = 0; r < cells.size(); ++r) {
            std::string text;
            for (std::size_t c = 0; c < cols; ++c) {
                auto cell = cells[r][c];
                if (width_of(cell) > widths[c]) cell = cut_columns(cell, 0, widths[c] - 1) + ellipsis;
                if (c > 0) text += sep;
                text += cell + std::string(static_cast<std::size_t>(widths[c] - width_of(cell)), ' ');
            }
            if (width_of(text) > spec_.width) text = cut_columns(text, 0, spec_.width);
            lines_.push_back({{{text, r == 0 ? Style::strong : Style::normal}}});
            if (r == 0) {
                std::string rule;
                for (std::size_t c = 0; c < cols; ++c) {
                    if (c > 0) rule += ascii() ? "-+-" : "─┼─";
                    rule += repeat(ascii() ? "-" : "─", widths[c]);
                }
                lines_.push_back({{{cut_columns(rule, 0, spec_.width), Style::dim}}});
            }
        }
    }

    void code(const Block& b) {
        std::string text = b.text;
        std::size_t at = 0;
        while (at < text.size()) {
            const auto end = text.find('\n', at);
            std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? text.size() : end + 1;
            const bool wide = width_of(line) > spec_.width;
            if (wide) line = cut_columns(line, spec_.hscroll, spec_.width - 1) + (ascii() ? "~" : "…");
            Line out;
            out.code = true;
            std::string word;
            auto flush = [&] {
                if (word.empty()) return;
                const bool key = std::find(kKeywords.begin(), kKeywords.end(), word) != kKeywords.end();
                out.spans.push_back({word, key ? Style::keyword : Style::code});
                word.clear();
            };
            for (char c : line) {
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                    word += c;
                    continue;
                }
                flush();
                out.spans.push_back({std::string(1, c), Style::code});
            }
            flush();
            lines_.push_back(std::move(out));
        }
    }

    const DocumentSpec& spec_;
    theory::LiveValues empty_;
    const theory::LiveValues* values_;
};

ftxui::Element span_element(const Span& s, const UiContext& ui, bool focused) {
    using namespace ftxui;
    const auto& th = ui.theme;
    auto e = text(s.text);
    switch (s.style) {
    case Style::dim: e = e | fg(th.dim, ui); break;
    case Style::strong: e = e | bold | fg(th.fg, ui); break;
    case Style::emphasis: e = e | italic | fg(th.fg, ui); break;
    case Style::code: e = e | fg(th.fg, ui); break;
    case Style::keyword: e = e | fg(th.warn, ui); break;
    case Style::heading: e = e | bold | fg(th.accent, ui); break;
    case Style::math: e = e | fg(th.fg, ui); break;
    case Style::ok: e = e | fg(th.ok, ui); break;
    case Style::err: e = e | fg(th.err, ui); break;
    case Style::link: e = focused ? e | bg(th.accent, ui) | fg(th.bg, ui) : e | fg(th.accent, ui); break;
    case Style::normal: e = e | fg(th.fg, ui); break;
    }
    return e;
}

}  // namespace

std::string cut_columns(const std::string& s, int from, int count) {
    const auto text = theory::to_u32(s);
    std::u32string out;
    int column = 0;
    for (char32_t c : text) {
        if (theory::is_combining(c)) {
            if (column > from && column <= from + count) out.push_back(c);
            continue;
        }
        if (column >= from && column < from + count) out.push_back(c);
        ++column;
    }
    return theory::to_utf8(out);
}

ftxui::Element document(const DocumentSpec& spec, const UiContext& ui, DocumentLayout* out) {
    using namespace ftxui;
    Builder b{spec};
    b.run();
    DocumentLayout local;
    auto& layout = out ? *out : local;
    layout = {};
    layout.height = static_cast<int>(b.lines_.size());
    layout.links = std::move(b.links_);
    layout.formulas = std::move(b.formulas_);
    for (const auto& l : b.lines_) {
        std::string plain;
        for (const auto& s : l.spans) plain += s.text;
        layout.lines.push_back(std::move(plain));
    }
    const auto code_bg = tint(ui.theme.bg, ui.theme.fg, 0.06);
    std::vector<bool> link_reflected(layout.links.size(), false);
    Elements rows;
    Elements formula_rows;
    int formula = -1;
    auto close_formula = [&] {
        if (formula >= 0 && !formula_rows.empty()) rows.push_back(vbox(std::move(formula_rows)) | reflect(layout.formulas[static_cast<std::size_t>(formula)].box));
        formula_rows.clear();
        formula = -1;
    };
    for (std::size_t i = static_cast<std::size_t>(std::max(0, spec.scroll)); i < b.lines_.size(); ++i) {
        const auto& l = b.lines_[i];
        Elements spans;
        for (const auto& s : l.spans) {
            const bool focused = s.link >= 0 && spec.focused_link && *spec.focused_link == static_cast<std::size_t>(s.link);
            auto e = span_element(s, ui, focused);
            if (s.link >= 0 && !link_reflected[static_cast<std::size_t>(s.link)]) {
                e = e | reflect(layout.links[static_cast<std::size_t>(s.link)].box);
                link_reflected[static_cast<std::size_t>(s.link)] = true;
            }
            spans.push_back(std::move(e));
        }
        auto row = spans.empty() ? text("") : hbox(std::move(spans));
        if (l.code) row = hbox({row, filler()}) | bg(code_bg, ui);
        if (l.formula != formula) close_formula();
        if (l.formula >= 0) {
            formula = l.formula;
            formula_rows.push_back(std::move(row));
            continue;
        }
        rows.push_back(std::move(row));
    }
    close_formula();
    return vbox(std::move(rows));
}

}  // namespace signals::tui
