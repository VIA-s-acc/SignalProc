#include "pipeline/sig.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"

namespace signals::pipeline {
namespace {

struct Failure {
    SigError error;
};

bool ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool ident_char(char c) { return ident_start(c) || (c >= '0' && c <= '9'); }
bool bare_char(char c) {
    return c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != ',' && c != '(' && c != ')' && c != '"' && c != '#' && c != '=';
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_{text} {}

    PipelineDoc run() {
        PipelineDoc doc;
        std::map<std::string, int, std::less<>> declared;
        for (;;) {
            skip(true);
            if (done()) break;
            const int line = line_, column = column_;
            if (!ident_start(peek())) fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.unknown_directive")), fmt::arg("word", rest_of_word())));
            const auto word = ident();
            if (word == "pipeline") {
                skip(false);
                doc.name = string_value();
                skip(false);
                const int vl = line_, vc = column_;
                const auto version = ident();
                if (version != "v1") fail(vl, vc, fmt::format(fmt::runtime(tr("pipeline.sig.version")), fmt::arg("version", version)));
                end_of_line();
            } else if (word == "node") {
                skip(false);
                NodeDecl node;
                node.line = line;
                const int il = line_, ic = column_;
                node.id = ident();
                if (const auto it = declared.find(node.id); it != declared.end())
                    fail(il, ic, fmt::format(fmt::runtime(tr("pipeline.sig.duplicate_node")), fmt::arg("node", node.id), fmt::arg("line", it->second)));
                declared.emplace(node.id, line);
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
                doc.nodes.push_back(std::move(node));
            } else if (word == "probe") {
                skip(false);
                ProbeDecl probe;
                probe.at = port_ref();
                skip(false);
                if (ident_start(peek())) {
                    const int al = line_, ac = column_;
                    if (ident() != "as") fail(al, ac, fmt::format(fmt::runtime(tr("pipeline.sig.expected_symbol")), fmt::arg("symbol", "as")));
                    skip(false);
                    probe.name = string_value();
                }
                end_of_line();
                doc.probes.push_back(std::move(probe));
            } else {
                PortRef prev{word, {}, line, column};
                if (peek() == '.') {
                    advance();
                    prev.port = ident();
                }
                skip(false);
                if (!arrow_next()) fail(line, column, fmt::format(fmt::runtime(tr("pipeline.sig.unknown_directive")), fmt::arg("word", word)));
                while (arrow_next()) {
                    const int al = line_, ac = column_;
                    advance();
                    advance();
                    skip(false);
                    if (done() || peek() == '\n' || !ident_start(peek())) fail(al, ac, tr("pipeline.sig.dangling_arrow"));
                    auto next = port_ref();
                    if (!prev.port.empty() && !doc.edges.empty() && doc.edges.back().to.node == prev.node && doc.edges.back().to.line == prev.line &&
                        doc.edges.back().to.column == prev.column)
                        fail(prev.line, prev.column, tr("pipeline.sig.port_in_chain"));
                    doc.edges.push_back({prev, next});
                    prev = std::move(next);
                    skip(false);
                }
                end_of_line();
            }
        }
        return doc;
    }

private:
    [[noreturn]] void fail(int line, int column, std::string message) { throw Failure{{line, column, std::move(message)}}; }

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

    void skip(bool newlines) {
        while (!done()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || (newlines && c == '\n')) {
                advance();
            } else if (c == '#') {
                while (!done() && peek() != '\n') advance();
            } else {
                break;
            }
        }
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

    Params params() {
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

    PortRef port_ref() {
        PortRef r;
        r.line = line_;
        r.column = column_;
        r.node = ident();
        if (peek() == '.') {
            advance();
            r.port = ident();
        }
        return r;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    int line_ = 1, column_ = 1;
};

bool bare_safe(const std::string& v) {
    if (v.empty()) return false;
    return std::all_of(v.begin(), v.end(), [](char c) { return ident_char(c) || c == '.' || c == '-' || c == '+'; });
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

std::string node_line(const NodeDecl& n, std::size_t id_width) {
    const auto* type = find_block_type(n.type);
    std::vector<std::pair<std::string, bool>> order;
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

std::string write_sig(const PipelineDoc& doc) {
    std::string out = "pipeline " + quoted(doc.name) + " v" + std::to_string(doc.version) + "\n";
    if (!doc.nodes.empty()) {
        std::size_t width = 0;
        for (const auto& n : doc.nodes) width = std::max(width, n.id.size());
        out += "\n";
        for (const auto& n : doc.nodes) out += node_line(n, width) + "\n";
    }
    if (doc.edges.empty() && doc.probes.empty()) return out;
    out += "\n";
    std::vector<PortRef> chain;
    const auto flush = [&] {
        if (chain.empty()) return;
        std::string line;
        for (const auto& r : chain) line += (line.empty() ? "" : " -> ") + ref_text(r);
        out += line + "\n";
        chain.clear();
    };
    for (const auto& e : doc.edges) {
        if (!chain.empty() && chain.back().port.empty() && e.from.port.empty() && chain.back().node == e.from.node) {
            chain.push_back(e.to);
            continue;
        }
        flush();
        chain = {e.from, e.to};
    }
    flush();
    for (const auto& p : doc.probes) out += "probe " + ref_text(p.at) + (p.name.empty() ? "" : " as " + quoted(p.name)) + "\n";
    return out;
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
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == target.size()) return bad();
    const std::string node_id{target.substr(0, dot)}, param{target.substr(dot + 1)};
    std::string value{assignment.substr(eq + 1)};
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    auto node = std::find_if(doc.nodes.begin(), doc.nodes.end(), [&](const NodeDecl& n) { return n.id == node_id; });
    if (node == doc.nodes.end()) {
        std::vector<std::string> ids;
        for (const auto& n : doc.nodes) ids.push_back(n.id);
        const auto hint = nearest_name(node_id, ids);
        return std::unexpected{set_error(hint ? fmt::format(fmt::runtime(tr("pipeline.set.unknown_node_hint")), fmt::arg("node", node_id), fmt::arg("hint", *hint))
                                              : fmt::format(fmt::runtime(tr("pipeline.set.unknown_node")), fmt::arg("node", node_id)))};
    }
    if (const auto* type = find_block_type(node->type)) {
        std::vector<std::string> names;
        for (const auto& spec : type->params) names.push_back(spec.name);
        if (std::find(names.begin(), names.end(), param) == names.end()) {
            const auto hint = nearest_name(param, names);
            return std::unexpected{set_error(
                hint ? fmt::format(fmt::runtime(tr("pipeline.error.unknown_param_hint")), fmt::arg("block", type->id), fmt::arg("param", param), fmt::arg("hint", *hint))
                     : fmt::format(fmt::runtime(tr("pipeline.error.unknown_param")), fmt::arg("block", type->id), fmt::arg("param", param)))};
        }
        auto trial = node->params;
        trial[param] = value;
        if (auto ok = resolve_params(*type, trial); !ok) return std::unexpected{ok.error()};
    }
    node->params[param] = std::move(value);
    return {};
}

}  // namespace signals::pipeline
