#include "pipeline/sig.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/flatten.hpp"
#include "pipeline/renames.hpp"

namespace signals::pipeline {
namespace {

struct Failure {
    SigError error;
};

bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }
bool directive_ahead(std::string_view word, std::string_view rest) {
    std::size_t i = 0;
    const auto spaces = [&] {
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
    };
    spaces();
    if (word == "use") return i < rest.size() && rest[i] == '"';
    if (word == "mark") return i < rest.size() && ((rest[i] >= '0' && rest[i] <= '9') || rest[i] == '$' || rest[i] == '.');
    if (word != "let" && word != "region" && word != "block") return false;
    if (i == 0 || i >= rest.size() || !ident_start(rest[i])) return false;
    while (i < rest.size() && ident_char(rest[i])) ++i;
    spaces();
    if (i >= rest.size()) return false;
    return word == "block" ? rest[i] == '(' || rest[i] == '{' : rest[i] == '=';
}

bool bare_char(char c) {
    return c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != ',' && c != '(' && c != ')' && c != '"' && c != '#' && c != '=';
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_{text} {}

    PipelineDoc run() {
        PipelineDoc doc;
        doc.version = 1;
        Names names;
        std::map<std::string, int, std::less<>> blocks;
        for (;;) {
            if (peek() == '\n') advance();
            leading();
            if (done()) break;
            const int line = line_, column = column_;
            if (!ident_start(peek())) fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.unknown_directive")), fmt::arg("word", rest_of_word())));
            const auto word = ident();
            if (word == "pipeline") {
                skip(false);
                doc.name = string_value();
                skip(false);
                const int vl = line_, vc = column_;
                doc.version = version_ = header_version(ident(), vl, vc);
                end_of_line();
                auto notes = take_comments();
                doc.preamble = std::move(notes.above);
                doc.header_comment = std::move(notes.after);
            } else if ((word == "block" || word == "use") && directive_ahead(word, rest_of_line())) {
                if (version_ < 2) fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.v2_directive")), fmt::arg("word", word)));
                if (word == "use") {
                    skip(false);
                    UseDecl use;
                    use.line = line;
                    use.path = string_value();
                    end_of_line();
                    use.comments = take_comments();
                    doc.uses.push_back(std::move(use));
                    continue;
                }
                auto def = block(line);
                if (const auto it = blocks.find(def.name); it != blocks.end())
                    fail(line, column, fmt::format(fmt::runtime(tr("pipeline.blocks.duplicate")), fmt::arg("name", def.name), fmt::arg("line", it->second)));
                blocks.emplace(def.name, line);
                doc.blocks.push_back(std::move(def));
            } else {
                statement(doc, names, word, line, column, true);
            }
        }
        for (const auto& b : doc.blocks) {
            const auto node = names.nodes.find(b.name);
            const auto let = names.lets.find(b.name);
            if (node != names.nodes.end() || let != names.lets.end())
                fail(b.line, 1, fmt::format(fmt::runtime(tr("pipeline.blocks.name_taken")), fmt::arg("name", b.name),
                                            fmt::arg("line", node != names.nodes.end() ? node->second : let->second)));
        }
        doc.tail = take_comments();
        while (!doc.tail.above.empty() && doc.tail.above.back().empty()) doc.tail.above.pop_back();
        if (doc.tail.above.empty()) doc.tail.gap = false;
        return doc;
    }

private:
    struct Names {
        std::map<std::string, int, std::less<>> nodes, lets;
    };

    [[noreturn]] void fail(int line, int column, std::string message) { throw Failure{{line, column, std::move(message)}}; }

    std::string path(std::string head, bool document) {
        while (peek() == '/') {
            if (!document) fail(line_, column_, tr("pipeline.blocks.slash_inside"));
            advance();
            head += "/" + ident();
        }
        return head;
    }

    template <class Body>
    void statement(Body& body, Names& names, const std::string& word, int line, int column, bool document) {
        if (word == "node") {
            skip(false);
            NodeDecl node;
            node.line = line;
            const int il = line_, ic = column_;
            node.id = ident();
            if (peek() == '/') fail(line_, column_, tr("pipeline.blocks.slash_in_name"));
            if (const auto it = names.nodes.find(node.id); it != names.nodes.end())
                fail(il, ic, fmt::format(fmt::runtime(tr("pipeline.sig.duplicate_node")), fmt::arg("node", node.id), fmt::arg("line", it->second)));
            names.nodes.emplace(node.id, line);
            skip(false);
            expect('=');
            skip(false);
            node.type = ident();
            skip(false);
            if (peek() == '(') node.params = params();
            skip(false);
            if (ident_start(peek())) {
                const int bl = line_, bc = column_;
                if (ident() != "bypass") fail(bl, bc, fmt::format(fmt::runtime(tr("pipeline.sig.expected_symbol")), fmt::arg("symbol", "bypass")));
                node.bypass = true;
            }
            end_of_line();
            node.comments = take_comments();
            body.nodes.push_back(std::move(node));
        } else if (word == "probe") {
            skip(false);
            ProbeDecl probe;
            probe.at = port_ref(document);
            skip(false);
            if (ident_start(peek())) {
                const int al = line_, ac = column_;
                if (ident() != "as") fail(al, ac, fmt::format(fmt::runtime(tr("pipeline.sig.expected_symbol")), fmt::arg("symbol", "as")));
                skip(false);
                probe.name = string_value();
            }
            end_of_line();
            probe.comments = take_comments();
            body.probes.push_back(std::move(probe));
        } else if (directive_ahead(word, rest_of_line())) {
            if (version_ < 2) fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.v2_directive")), fmt::arg("word", word)));
            if (!document && word != "let") fail(line, column, fmt::format(fmt::runtime(tr("pipeline.blocks.nested")), fmt::arg("word", word)));
            if (word != "let") fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.unknown_directive")), fmt::arg("word", word)));
            skip(false);
            LetDecl let;
            let.line = line;
            const int nl = line_, nc = column_;
            let.name = ident();
            if (const auto it = names.lets.find(let.name); it != names.lets.end())
                fail(nl, nc, fmt::format(fmt::runtime(tr("pipeline.vars.duplicate")), fmt::arg("name", let.name), fmt::arg("line", it->second)));
            names.lets.emplace(let.name, line);
            skip(false);
            expect('=');
            skip(false);
            if (done() || peek() == '\n') fail(line_, column_, tr("pipeline.sig.expected_value"));
            let.value = peek() == '"' ? string_value() : bare_value();
            end_of_line();
            let.comments = take_comments();
            body.lets.push_back(std::move(let));
        } else {
            PortRef prev{path(word, document), {}, line, column};
            if (peek() == '.') {
                advance();
                prev.port = ident();
            }
            skip(false);
            if (!arrow_next()) fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.unknown_directive")), fmt::arg("word", word)));
            const auto first = body.edges.size();
            while (arrow_next()) {
                const int al = line_, ac = column_;
                advance();
                advance();
                skip(false);
                if (done() || peek() == '\n' || !ident_start(peek())) fail(al, ac, tr("pipeline.sig.dangling_arrow"));
                auto next = port_ref(document);
                if (!prev.port.empty() && !body.edges.empty() && body.edges.back().to.node == prev.node && body.edges.back().to.line == prev.line &&
                    body.edges.back().to.column == prev.column)
                    fail(prev.line, prev.column, tr("pipeline.sig.port_in_chain"));
                body.edges.push_back({prev, next});
                prev = std::move(next);
                skip(false);
            }
            end_of_line();
            body.edges[first].comments = take_comments();
        }
    }

    BlockDef block(int line) {
        BlockDef def;
        def.line = line;
        skip(false);
        def.name = ident();
        skip(false);
        if (peek() == '(') def.defaults = params(&def.order);
        skip(false);
        expect('{');
        end_of_line();
        def.comments = take_comments();
        Names names;
        std::map<std::string, int, std::less<>> ports;
        for (;;) {
            if (peek() == '\n') advance();
            leading();
            if (done()) fail(line, 1, fmt::format(fmt::runtime(tr("pipeline.blocks.unclosed")), fmt::arg("name", def.name)));
            const int sl = line_, sc = column_;
            if (peek() == '}') {
                advance();
                end_of_line();
                def.closing = take_comments();
                break;
            }
            if (!ident_start(peek())) fail(sl, sc, fmt::format(fmt::runtime(tr("pipeline.sig.unknown_directive")), fmt::arg("word", rest_of_word())));
            const auto word = ident();
            if ((word == "block" || word == "use" || word == "region" || word == "mark") && directive_ahead(word, rest_of_line()))
                fail(sl, sc, fmt::format(fmt::runtime(tr("pipeline.blocks.nested")), fmt::arg("word", word)));
            const auto after = rest_of_line();
            const auto first = after.find_first_not_of(" \t");
            const bool port_line = (word == "in" || word == "out") && first != std::string_view::npos && first > 0 && ident_start(after[first]);
            if (!port_line) {
                statement(def, names, word, sl, sc, false);
                continue;
            }
            auto& list = word == "in" ? def.ins : def.outs;
            const auto at = list.size();
            for (;;) {
                skip(false);
                PortDecl port;
                port.line = sl;
                const int pl = line_, pc = column_;
                port.name = ident();
                if (const auto it = ports.find(port.name); it != ports.end())
                    fail(pl, pc, fmt::format(fmt::runtime(tr("pipeline.blocks.duplicate_port")), fmt::arg("name", port.name), fmt::arg("line", it->second)));
                ports.emplace(port.name, sl);
                skip(false);
                if (word == "in" && ident_start(peek())) {
                    const int ol = line_, oc = column_;
                    if (ident() != "optional") fail(ol, oc, fmt::format(fmt::runtime(tr("pipeline.sig.expected_symbol")), fmt::arg("symbol", "optional")));
                    port.optional = true;
                    skip(false);
                }
                list.push_back(std::move(port));
                if (peek() != ',') break;
                advance();
            }
            end_of_line();
            list[at].comments = take_comments();
        }
        for (const auto& [name, at] : ports)
            if (const auto it = names.nodes.find(name); it != names.nodes.end())
                fail(it->second, 1, fmt::format(fmt::runtime(tr("pipeline.blocks.port_clash")), fmt::arg("name", name)));
        return def;
    }


    bool done() const { return pos_ >= text_.size(); }
    char peek(std::size_t ahead = 0) const { return pos_ + ahead < text_.size() ? text_[pos_ + ahead] : '\0'; }
    bool arrow_next() const { return peek() == '-' && peek(1) == '>'; }

    void advance() {
        if (done()) return;
        if (text_[pos_] == '\n') {
            ++line_;
            column_ = 1;
            ++pos_;
            return;
        }
        ++pos_;
        while (!done() && (static_cast<unsigned char>(text_[pos_]) & 0xC0) == 0x80) ++pos_;
        ++column_;
    }

    std::string comment() {
        const auto start = pos_;
        while (!done() && peek() != '\n') advance();
        auto text = text_.substr(start, pos_ - start);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
        return std::string{text};
    }

    void skip(bool newlines) {
        while (!done()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || (newlines && c == '\n')) {
                advance();
            } else if (c == '#') {
                auto text = comment();
                if (newlines) {
                    pending_.push_back(std::move(text));
                } else {
                    if (!trailing_.empty()) pending_.push_back(std::move(trailing_));
                    trailing_ = std::move(text);
                }
            } else {
                break;
            }
        }
    }

    void leading() {
        while (!done()) {
            while (!done() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) advance();
            if (done()) return;
            if (peek() == '\n') {
                advance();
                if (!pending_.empty()) pending_.push_back({});
                else gap_ = started_;
            } else if (peek() == '#') {
                pending_.push_back(comment());
                if (!done()) advance();
            } else {
                return;
            }
        }
    }

    Comments take_comments() {
        Comments out{gap_, std::move(pending_), std::move(trailing_)};
        pending_.clear();
        trailing_.clear();
        gap_ = false;
        started_ = true;
        return out;
    }

    std::string_view rest_of_line() const {
        const auto end = text_.find('\n', pos_);
        return text_.substr(pos_, end == std::string_view::npos ? std::string_view::npos : end - pos_);
    }

    int header_version(const std::string& word, int line, int column) {
        int number = 0;
        const bool numeric = word.size() > 1 && word.front() == 'v' &&
                             std::from_chars(word.data() + 1, word.data() + word.size(), number).ptr == word.data() + word.size();
        if (numeric && number >= 1 && number <= kSigVersion) return number;
        if (numeric && number > kSigVersion)
            fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.version_newer")), fmt::arg("found", word), fmt::arg("max", "v" + std::to_string(kSigVersion))));
        fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.version")), fmt::arg("version", word)));
    }

    std::string rest_of_word() const {
        std::size_t end = pos_;
        while (end < text_.size() && bare_char(text_[end])) ++end;
        return std::string{text_.substr(pos_, std::max<std::size_t>(1, end - pos_))};
    }

    std::string ident() {
        if (!ident_start(peek())) fail(line_, column_, tr("pipeline.sig.expected_name"));
        std::string out;
        while (!done() && ident_char(peek())) {
            out += peek();
            advance();
        }
        return out;
    }

    void expect(char c) {
        if (peek() != c) fail(line_, column_, fmt::format(fmt::runtime(tr("pipeline.sig.expected_symbol")), fmt::arg("symbol", std::string(1, c))));
        advance();
    }

    void end_of_line() {
        skip(false);
        if (!done() && peek() != '\n') fail(line_, column_, fmt::format(fmt::runtime(tr("pipeline.sig.trailing")), fmt::arg("text", rest_of_word())));
    }

    std::string string_value() {
        if (peek() != '"') fail(line_, column_, tr("pipeline.sig.expected_string"));
        const int ql = line_, qc = column_;
        advance();
        std::string out;
        for (;;) {
            if (done() || peek() == '\n') fail(ql, qc, tr("pipeline.sig.unclosed_string"));
            const char c = peek();
            if (c == '"') {
                advance();
                return out;
            }
            if (c == '\\' && (peek(1) == '"' || peek(1) == '\\')) {
                out += peek(1);
                advance();
                advance();
                continue;
            }
            const std::size_t start = pos_;
            advance();
            out += text_.substr(start, pos_ - start);
        }
    }

    std::string bare_value() {
        const std::size_t start = pos_;
        while (!done() && bare_char(peek())) advance();
        if (pos_ == start) fail(line_, column_, tr("pipeline.sig.expected_value"));
        return std::string{text_.substr(start, pos_ - start)};
    }

    Params params(std::vector<std::string>* order = nullptr) {
        const int pl = line_, pc = column_;
        advance();
        Params out;
        for (;;) {
            skip(true);
            if (done()) fail(pl, pc, tr("pipeline.sig.unclosed_paren"));
            if (peek() == ')') {
                advance();
                return out;
            }
            const int kl = line_, kc = column_;
            const auto key = ident();
            skip(true);
            if (done()) fail(pl, pc, tr("pipeline.sig.unclosed_paren"));
            expect('=');
            skip(true);
            if (done()) fail(pl, pc, tr("pipeline.sig.unclosed_paren"));
            auto value = peek() == '"' ? string_value() : bare_value();
            if (!out.emplace(key, std::move(value)).second)
                fail(kl, kc, fmt::format(fmt::runtime(tr("pipeline.sig.duplicate_param")), fmt::arg("param", key)));
            if (order) order->push_back(key);
            skip(true);
            if (done()) fail(pl, pc, tr("pipeline.sig.unclosed_paren"));
            if (peek() == ',') {
                advance();
                continue;
            }
            if (peek() != ')') {
                if (ident_start(peek())) fail(pl, pc, tr("pipeline.sig.unclosed_paren"));
                fail(line_, column_, fmt::format(fmt::runtime(tr("pipeline.sig.expected_symbol")), fmt::arg("symbol", ")")));
            }
        }
    }

    PortRef port_ref(bool document) {
        PortRef r;
        r.line = line_;
        r.column = column_;
        r.node = path(ident(), document);
        if (peek() == '.') {
            advance();
            r.port = ident();
        }
        return r;
    }

    std::string_view text_;
    int version_ = 1;
    std::size_t pos_ = 0;
    int line_ = 1, column_ = 1;
    std::vector<std::string> pending_;
    std::string trailing_;
    bool gap_ = false, started_ = false;
};

bool bare_safe(const std::string& v) {
    if (v.empty()) return false;
    return std::all_of(v.begin(), v.end(), [](char c) { return ident_char(c) || c == '.' || c == '-' || c == '+' || c == '$'; });
}

std::string quoted(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string ref_text(const PortRef& r) { return r.port.empty() ? r.node : r.node + "." + r.port; }

const BlockDef* own_block(const PipelineDoc* doc, std::string_view name) {
    if (!doc) return nullptr;
    for (const auto* list : {&doc->blocks, &doc->imported})
        for (const auto& b : *list)
            if (b.name == name) return &b;
    return nullptr;
}

std::string node_line(const NodeDecl& n, std::size_t id_width, const PipelineDoc* doc = nullptr) {
    const auto* own = own_block(doc, n.type);
    const auto* type = own ? nullptr : find_block_type(n.type);
    std::vector<std::pair<std::string, bool>> order;
    if (own)
        for (const auto& key : own->order)
            if (n.params.contains(key)) order.emplace_back(key, false);
    if (type)
        for (const auto& spec : type->params)
            if (n.params.contains(spec.name)) order.emplace_back(spec.name, spec.kind == ParamSpec::Kind::path);
    for (const auto& [key, value] : n.params)
        if (std::none_of(order.begin(), order.end(), [&](const auto& o) { return o.first == key; })) order.emplace_back(key, false);
    std::string args;
    for (const auto& [key, path] : order) {
        const auto& value = n.params.find(key)->second;
        args += (args.empty() ? "" : ", ") + key + "=" + (path || !bare_safe(value) ? quoted(value) : value);
    }
    return "node " + n.id + std::string(id_width - n.id.size(), ' ') + " = " + n.type + "(" + args + ")" + (n.bypass ? " bypass" : "");
}

std::size_t distance(std::string_view a, std::string_view b) {
    const auto n = a.size(), m = b.size();
    std::vector<std::vector<std::size_t>> d(n + 1, std::vector<std::size_t>(m + 1));
    for (std::size_t i = 0; i <= n; ++i) d[i][0] = i;
    for (std::size_t j = 0; j <= m; ++j) d[0][j] = j;
    for (std::size_t i = 1; i <= n; ++i)
        for (std::size_t j = 1; j <= m; ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[n][m];
}

core::Error set_error(std::string message) { return {core::Errc::invalid_argument, std::move(message)}; }

}  // namespace

std::expected<PipelineDoc, SigError> parse_sig(std::string_view text) {
    try {
        return Parser{text}.run();
    } catch (const Failure& f) {
        return std::unexpected{f.error};
    }
}

std::string unescape_dollars(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        out += value[i];
        if (value[i] == '$' && i + 1 < value.size() && value[i + 1] == '$') ++i;
    }
    return out;
}

std::string escape_dollars(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) out += c == '$' ? std::string_view{"$$"} : std::string_view{&c, 1};
    return out;
}

std::string after_text(const std::string& comment) { return comment.empty() ? std::string{} : "  " + comment; }

void write_wires(std::string& out, const std::vector<Edge>& edges, const std::vector<ProbeDecl>& probes, const std::string& indent,
                 const std::function<void(const Comments&)>& above) {
    std::vector<PortRef> chain;
    std::string chain_comment;
    const auto flush = [&] {
        if (chain.empty()) return;
        std::string line;
        for (const auto& r : chain) line += (line.empty() ? "" : " -> ") + ref_text(r);
        out += indent + line + after_text(chain_comment) + "\n";
        chain.clear();
    };
    for (const auto& e : edges) {
        const bool joins = !chain.empty() && chain.back().port.empty() && e.from.port.empty() && chain.back().node == e.from.node;
        if (joins && e.comments.empty() && (chain_comment.empty() || e.from.line == chain.back().line)) {
            chain.push_back(e.to);
            continue;
        }
        flush();
        above(e.comments);
        chain = {e.from, e.to};
        chain_comment = e.comments.after;
    }
    flush();
    for (const auto& p : probes) {
        above(p.comments);
        out += indent + "probe " + ref_text(p.at) + (p.name.empty() ? "" : " as " + quoted(p.name)) + after_text(p.comments.after) + "\n";
    }
}

void write_block(std::string& out, const BlockDef& b, const PipelineDoc& doc) {
    const std::string in = "    ";
    std::string head = "block " + b.name;
    if (!b.order.empty()) {
        std::string args;
        for (const auto& key : b.order) {
            const auto& value = b.defaults.at(key);
            args += (args.empty() ? "" : ", ") + key + "=" + (bare_safe(value) ? value : quoted(value));
        }
        head += "(" + args + ")";
    }
    out += head + " {" + after_text(b.comments.after) + "\n";
    bool any = false, first = true;
    const auto line_of = [&](const std::string& text) { out += (text.empty() ? std::string{} : in + text) + "\n"; };
    const auto above = [&](const Comments& c) {
        if (c.gap && !first) out += "\n";
        for (const auto& line : c.above) line_of(line);
        first = false;
    };
    const auto ports = [&](const std::vector<PortDecl>& list, const char* word) {
        for (std::size_t k = 0; k < list.size();) {
            above(list[k].comments);
            std::string line = std::string{word} + " ";
            auto j = k;
            for (; j < list.size() && list[j].line == list[k].line; ++j) line += (j > k ? ", " : "") + list[j].name + (list[j].optional ? " optional" : "");
            line_of(line + after_text(list[k].comments.after));
            k = j;
            any = true;
        }
    };
    ports(b.ins, "in");
    ports(b.outs, "out");
    for (const auto& l : b.lets) {
        above(l.comments);
        line_of("let " + l.name + " = " + (bare_safe(l.value) ? l.value : quoted(l.value)) + after_text(l.comments.after));
        any = true;
    }
    if (!b.nodes.empty()) {
        if (any) out += "\n";
        first = true;
        std::size_t width = 0;
        for (const auto& n : b.nodes) width = std::max(width, n.id.size());
        for (const auto& n : b.nodes) {
            above(n.comments);
            line_of(node_line(n, width, &doc) + after_text(n.comments.after));
        }
        any = true;
    }
    if (!b.edges.empty() || !b.probes.empty()) {
        if (any) out += "\n";
        first = true;
        write_wires(out, b.edges, b.probes, in, above);
    }
    if (b.closing.gap && !b.closing.above.empty()) out += "\n";
    for (const auto& line : b.closing.above) line_of(line);
    out += "}" + after_text(b.closing.after) + "\n";
}

std::string block_text(const BlockDef& block, const PipelineDoc& doc) {
    std::string out;
    write_block(out, block, doc);
    return out;
}

const BlockDef* find_own_block(const PipelineDoc& doc, std::string_view name) { return own_block(&doc, name); }

std::string write_sig(const PipelineDoc& doc) {
    std::string out;
    for (const auto& line : doc.preamble) out += line + "\n";
    const auto after = [](const std::string& comment) { return comment.empty() ? std::string{} : "  " + comment; };
    out += "pipeline " + quoted(doc.name) + " v" + std::to_string(doc.version) + after(doc.header_comment) + "\n";
    bool first = true;
    const auto above = [&](const Comments& c) {
        if (c.gap && !first) out += "\n";
        for (const auto& line : c.above) out += line + "\n";
        first = false;
    };
    if (!doc.uses.empty()) {
        out += "\n";
        first = true;
        for (const auto& u : doc.uses) {
            above(u.comments);
            out += "use " + quoted(u.path) + after(u.comments.after) + "\n";
        }
    }
    if (!doc.lets.empty()) {
        out += "\n";
        first = true;
        for (const auto& l : doc.lets) {
            above(l.comments);
            out += "let " + l.name + " = " + (bare_safe(l.value) ? l.value : quoted(l.value)) + after(l.comments.after) + "\n";
        }
    }
    if (!doc.blocks.empty()) {
        out += "\n";
        first = true;
        for (const auto& b : doc.blocks) {
            above(b.comments);
            write_block(out, b, doc);
        }
    }
    if (!doc.nodes.empty()) {
        std::size_t width = 0;
        for (const auto& n : doc.nodes) width = std::max(width, n.id.size());
        out += "\n";
        first = true;
        for (const auto& n : doc.nodes) {
            above(n.comments);
            out += node_line(n, width, &doc) + after(n.comments.after) + "\n";
        }
    }
    if (!doc.edges.empty() || !doc.probes.empty()) {
        out += "\n";
        first = true;
        write_wires(out, doc.edges, doc.probes, "", above);
    }
    if (!doc.tail.above.empty()) {
        if (doc.tail.gap) out += "\n";
        for (const auto& line : doc.tail.above) out += line + "\n";
    }
    return out;
}

void remove_edges(PipelineDoc& doc, const std::function<bool(const Edge&)>& gone) {
    for (std::size_t k = 0; k < doc.edges.size(); ++k) {
        auto& e = doc.edges[k];
        if (e.comments.empty() || e.from.line == 0 || !gone(e)) continue;
        for (std::size_t j = k + 1; j < doc.edges.size() && doc.edges[j].from.line == e.from.line && doc.edges[j].comments.empty(); ++j)
            if (!gone(doc.edges[j])) {
                doc.edges[j].comments = std::move(e.comments);
                e.comments = {};
                break;
            }
    }
    std::erase_if(doc.edges, gone);
}

void remove_node(PipelineDoc& doc, std::string_view id) {
    const auto it = std::find_if(doc.nodes.begin(), doc.nodes.end(), [&](const NodeDecl& n) { return n.id == id; });
    if (it == doc.nodes.end()) return;
    if (it->comments.gap && std::next(it) != doc.nodes.end()) std::next(it)->comments.gap = true;
    doc.nodes.erase(it);
    remove_edges(doc, [&](const Edge& e) { return e.from.node == id || e.to.node == id; });
    std::erase_if(doc.probes, [&](const ProbeDecl& p) { return p.at.node == id; });
}

std::optional<std::string> nearest_name(std::string_view name, const std::vector<std::string>& candidates) {
    std::optional<std::string> best;
    std::size_t best_distance = std::max<std::size_t>(1, (name.size() + 2) / 3) + 1;
    for (const auto& c : candidates) {
        const auto d = distance(name, c);
        if (d < best_distance) {
            best_distance = d;
            best = c;
        }
    }
    return best;
}

core::Result<void> apply_set(PipelineDoc& doc, std::string_view assignment) {
    const auto bad = [&] { return std::unexpected{set_error(fmt::format(fmt::runtime(tr("pipeline.set.bad")), fmt::arg("text", assignment)))}; };
    const auto eq = assignment.find('=');
    if (eq == std::string_view::npos) return bad();
    const auto target = assignment.substr(0, eq);
    const auto dot = target.find('.');
    if (dot == std::string_view::npos && !target.empty() && doc.version >= 2) {
        std::string value{assignment.substr(eq + 1)};
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        for (auto& l : doc.lets)
            if (l.name == target) {
                l.value = std::move(value);
                return {};
            }
        std::vector<std::string> names;
        for (const auto& l : doc.lets) names.push_back(l.name);
        const auto hint = nearest_name(target, names);
        return std::unexpected{set_error(hint ? fmt::format(fmt::runtime(tr("pipeline.set.unknown_variable_hint")), fmt::arg("name", target), fmt::arg("hint", *hint))
                                              : fmt::format(fmt::runtime(tr("pipeline.set.unknown_variable")), fmt::arg("name", target)))};
    }
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == target.size()) return bad();
    const std::string node_id{target.substr(0, dot)}, param{target.substr(dot + 1)};
    std::string value{assignment.substr(eq + 1)};
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    if (const auto slash = node_id.find('/'); slash != std::string::npos)
        return std::unexpected{set_error(fmt::format(fmt::runtime(tr("pipeline.blocks.inner_set")), fmt::arg("node", node_id), fmt::arg("call", node_id.substr(0, slash))))};
    auto node = std::find_if(doc.nodes.begin(), doc.nodes.end(), [&](const NodeDecl& n) { return n.id == node_id; });
    if (node == doc.nodes.end()) {
        std::vector<std::string> ids;
        for (const auto& n : doc.nodes) ids.push_back(n.id);
        const auto hint = nearest_name(node_id, ids);
        return std::unexpected{set_error(hint ? fmt::format(fmt::runtime(tr("pipeline.set.unknown_node_hint")), fmt::arg("node", node_id), fmt::arg("hint", *hint))
                                              : fmt::format(fmt::runtime(tr("pipeline.set.unknown_node")), fmt::arg("node", node_id)))};
    }
    if (const auto* def = own_block(&doc, node->type)) {
        if (!def->defaults.contains(param)) {
            const auto hint = nearest_name(param, def->order);
            return std::unexpected{set_error(
                hint ? fmt::format(fmt::runtime(tr("pipeline.blocks.unknown_param_hint")), fmt::arg("block", def->name), fmt::arg("param", param), fmt::arg("hint", *hint))
                     : fmt::format(fmt::runtime(tr("pipeline.blocks.unknown_param")), fmt::arg("block", def->name), fmt::arg("param", param)))};
        }
        node->params[param] = std::move(value);
        return {};
    }
    if (const auto* type = find_block_type(node->type)) {
        std::vector<std::string> names;
        for (const auto& spec : type->params) names.push_back(spec.name);
        if (std::find(names.begin(), names.end(), param) == names.end()) {
            if (auto now = renamed(v1_renames(), Rename::Kind::param, type->id, param))
                return std::unexpected{set_error(fmt::format(fmt::runtime(tr("pipeline.set.renamed")), fmt::arg("block", type->id), fmt::arg("old", param), fmt::arg("new", *now)))};
            const auto hint = nearest_name(param, names);
            return std::unexpected{set_error(
                hint ? fmt::format(fmt::runtime(tr("pipeline.error.unknown_param_hint")), fmt::arg("block", type->id), fmt::arg("param", param), fmt::arg("hint", *hint))
                     : fmt::format(fmt::runtime(tr("pipeline.error.unknown_param")), fmt::arg("block", type->id), fmt::arg("param", param)))};
        }
        auto trial = node->params;
        trial[param] = value;
        if (const auto scope = resolve_lets(doc.lets))
            for (auto& [key, text] : trial) {
                const auto spec = std::ranges::find(type->params, key, &ParamSpec::name);
                if (spec == type->params.end() || text.find('$') == std::string::npos) continue;
                if (auto done = substitute(text, *scope, spec->kind)) text = std::move(*done);
            }
        if (auto ok = resolve_params(*type, trial); !ok) return std::unexpected{ok.error()};
    }
    node->params[param] = std::move(value);
    return {};
}

}  // namespace signals::pipeline
