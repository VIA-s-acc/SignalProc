#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "core/design/families.hpp"
#include "core/expr.hpp"
#include "i18n/i18n.hpp"
#include "pipeline/flatten.hpp"
#include "pipeline/sig.hpp"
#include "pipeline/npz_values.hpp"
#include "python/form.hpp"

struct SignalsEmbeddedFile {
    const char* name;
    const char* text;
};
extern const SignalsEmbeddedFile kSignalsPythonFunctions[];
extern const std::size_t kSignalsPythonFunctionsCount;

namespace signals::python {
namespace {

const std::set<std::string, std::less<>> kReserved{
    "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del", "elif", "else", "except", "finally",
    "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield",
    "match", "case", "type", "abs", "all", "any", "ascii", "bin", "bool", "breakpoint", "bytearray", "bytes", "callable", "chr", "classmethod",
    "compile", "complex", "delattr", "dict", "dir", "divmod", "enumerate", "eval", "exec", "exit", "filter", "float", "format", "frozenset",
    "getattr", "globals", "hasattr", "hash", "help", "hex", "id", "input", "int", "isinstance", "issubclass", "iter", "len", "list", "locals", "map",
    "max", "memoryview", "min", "next", "object", "oct", "open", "ord", "pow", "print", "property", "quit", "range", "repr", "reversed", "round",
    "set", "setattr", "slice", "sorted", "staticmethod", "str", "sum", "super", "tuple", "vars", "zip", "np", "pywt", "signal", "scipy", "Path",
    "sys", "math", "wavfile", "D", "HERE", "GRAPH", "verify", "verify_text", "verify_name", "verify_db", "report", "plt", "Audio"};

}  // namespace

std::span<const FunctionFile> python_functions() {
    static const std::vector<FunctionFile> all = [] {
        std::vector<FunctionFile> out;
        for (std::size_t k = 0; k < kSignalsPythonFunctionsCount; ++k) {
            std::string_view name{kSignalsPythonFunctions[k].name};
            if (name.ends_with(".py")) name.remove_suffix(3);
            out.push_back({name, kSignalsPythonFunctions[k].text});
        }
        return out;
    }();
    return all;
}

const FunctionFile* find_python_function(std::string_view name) {
    for (const auto& f : python_functions())
        if (f.name == name) return &f;
    return nullptr;
}

std::vector<std::string> function_needs(std::string_view text) {
    std::vector<std::string> out;
    constexpr std::string_view tag = "# needs:";
    while (text.starts_with("# ") && !text.starts_with(tag)) {
        const auto end = text.find('\n');
        if (end == std::string_view::npos) return out;
        text.remove_prefix(end + 1);
    }
    if (!text.starts_with(tag)) return out;
    auto line = text.substr(tag.size(), text.find('\n') - tag.size());
    std::string word;
    for (const char c : line) {
        if (c == ',' || c == ' ') {
            if (!word.empty()) out.push_back(std::move(word)), word.clear();
        } else if (c != '\r') {
            word += c;
        }
    }
    if (!word.empty()) out.push_back(word);
    return out;
}

std::vector<std::string> function_imports(std::string_view text) {
    std::vector<std::string> out;
    constexpr std::string_view tag = "# import: ";
    while (text.starts_with("# ")) {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        if (line.starts_with(tag)) out.emplace_back(line.substr(tag.size()));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return out;
}

std::vector<std::string> import_bindings(std::string_view line) {
    std::vector<std::string> out;
    const bool from = line.starts_with("from ");
    const auto at = from ? line.find(" import ") : std::string_view::npos;
    auto rest = from ? (at == std::string_view::npos ? std::string_view{} : line.substr(at + 8)) : line.substr(line.starts_with("import ") ? 7 : line.size());
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        auto part = rest.substr(0, comma);
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        while (part.starts_with(' ')) part.remove_prefix(1);
        while (part.ends_with(' ')) part.remove_suffix(1);
        if (const auto as = part.find(" as "); as != std::string_view::npos) part = part.substr(as + 4);
        else if (!from) part = part.substr(0, part.find('.'));
        if (!part.empty()) out.emplace_back(part);
    }
    return out;
}

std::span<const std::string_view> temporary_suffixes() {
    static constexpr std::string_view kSuffixes[] = {"a",    "angle", "block", "delays", "f",    "fs",   "gains", "h",    "integral", "mid", "mono", "n",
                                                     "ok",   "only",  "meta",  "path",  "paths",  "rms",  "scale", "side", "size", "sos",      "t",   "v",    "value"};
    return kSuffixes;
}

namespace {

const std::set<std::string, std::less<>>& script_names() {
    static const std::set<std::string, std::less<>> names = [] {
        std::set<std::string, std::less<>> out{"signal", "pywt", "scipy", "Audio", "display"};
        for (const auto& f : python_functions()) {
            for (const auto& line : function_imports(f.text))
                for (auto& name : import_bindings(line)) out.insert(std::move(name));
            for (std::size_t at = 0; at < f.text.size();) {
                const auto end = std::min(f.text.find('\n', at), f.text.size());
                const auto line = f.text.substr(at, end - at);
                std::size_t k = line.starts_with("def ") ? 4 : 0;
                const auto first = k;
                while (k < line.size() && (std::isalnum(static_cast<unsigned char>(line[k])) || line[k] == '_')) ++k;
                if (k > first && (first == 4 ? line.substr(k).starts_with("(") : line.substr(k).starts_with(" = "))) out.emplace(line.substr(first, k - first));
                at = end + 1;
            }
        }
        return out;
    }();
    return names;
}

}  // namespace

const std::set<std::string, std::less<>>& by_filter_bank() {
    static const std::set<std::string, std::less<>> names{"sym2", "sym3", "sym4", "sym5", "sym6", "sym7", "sym8", "sym9", "sym10", "sym12", "sym13", "sym17"};
    return names;
}

std::string py_name(std::string_view id, const std::set<std::string, std::less<>>& taken) {
    std::string name{id};
    while (kReserved.contains(name) || taken.contains(name) || find_python_function(name) || script_names().contains(name)) name += '_';
    return name;
}

std::string py_str(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (u < 0x20) out += fmt::format("\\x{:02x}", u);
        else out += c;
    }
    return out + "\"";
}

std::string py_comment(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto u = static_cast<unsigned char>(text[i]);
        if (u < 0x20 || u == 0x7f) {
            out += ' ';
        } else if (u == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(text[i + 2]) == 0xA8 || static_cast<unsigned char>(text[i + 2]) == 0xA9)) {
            out += ' ';
            i += 2;
        } else if (u == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x85) {
            out += ' ';
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::string Emitter::name_for(std::string_view id) {
    const auto clashes = [&](const std::string& v) {
        return std::ranges::any_of(temporary_suffixes(), [&](std::string_view s) { return taken_.contains(v + "_" + std::string{s}); });
    };
    auto name = py_name(id, taken_);
    while (clashes(name)) name = py_name(name + "_", taken_);
    taken_.insert(name);
    for (const auto s : temporary_suffixes()) taken_.insert(name + "_" + std::string{s});
    return name;
}

void Emitter::put(std::string key, io::NpArray value) {
    for (auto& [k, v] : entries_)
        if (k == key) return void(v = std::move(value));
    entries_.emplace_back(std::move(key), std::move(value));
}

std::string Emitter::data(std::string key, io::NpArray value) {
    auto full = node_.empty() ? std::move(key) : node_ + "." + key;
    if (!preview_) put(full, std::move(value));
    return "D[" + py_str(full) + "]";
}

void Emitter::import_line(std::string_view line) {
    if (std::find(imports_.begin(), imports_.end(), line) == imports_.end()) imports_.emplace_back(line);
}

void Emitter::function(std::string_view name) {
    const auto* f = find_python_function(name);
    if (!f) {
        if (std::find(missing_.begin(), missing_.end(), name) == missing_.end()) missing_.emplace_back(name);
        return;
    }
    if (std::find(functions_.begin(), functions_.end(), name) != functions_.end()) return;
    functions_.emplace_back(name);
    for (const auto& line : function_imports(f->text)) import_line(line);
    for (const auto& need : function_needs(f->text)) function(need);
}

std::vector<std::string> Emitter::functions() const {
    std::vector<std::string> order, texts;
    std::function<void(std::string_view)> visit = [&](std::string_view name) {
        if (std::find(order.begin(), order.end(), name) != order.end()) return;
        const auto* f = find_python_function(name);
        if (!f) return;
        order.emplace_back(name);
        for (const auto& need : function_needs(f->text)) visit(need);
        texts.emplace_back(f->text);
    };
    for (const auto& name : functions_) visit(name);
    return texts;
}

std::string Emitter::number(double v) const {
    if (std::isnan(v)) return "np.nan";
    if (std::isinf(v)) return v > 0 ? "np.inf" : "-np.inf";
    return fmt::format("{}", v);
}

std::string Emitter::variable(std::string_view sig_name, bool integer_literal) {
    auto name = py_name(sig_name, taken_);
    taken_.insert(name);
    vars_.insert_or_assign(std::string{sig_name}, name);
    if (integer_literal) int_vars_.insert(std::string{sig_name});
    return name;
}

std::string Emitter::py_var(std::string_view sig_name) const {
    if (const auto it = vars_.find(sig_name); it != vars_.end()) return it->second;
    return py_name(sig_name, {});
}

namespace {

struct PyText {
    std::string text;
    int prec = 9;
};

std::string wrap(const PyText& t, int at_least) { return t.prec >= at_least ? t.text : "(" + t.text + ")"; }

}  // namespace

std::optional<std::string> Emitter::py_expression(std::string_view raw) const {
    if (raw.find("$$") != std::string_view::npos) return std::nullopt;
    std::vector<std::string> names;
    std::string text;
    for (std::size_t at = 0; at < raw.size(); ++at) {
        if (raw[at] != '$') {
            text += raw[at];
            continue;
        }
        auto end = at + 1;
        while (end < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[end])) || raw[end] == '_')) ++end;
        if (end == at + 1) return std::nullopt;
        text += "_v" + std::to_string(names.size()) + " ";
        names.emplace_back(raw.substr(at + 1, end - at - 1));
        at = end - 1;
    }
    const auto parsed = core::parse_expr(text);
    if (!parsed) return std::nullopt;
    const auto& e = *parsed;
    using Op = core::Expr::Op;
    bool ok = true;
    std::function<PyText(std::size_t)> emit = [&](std::size_t i) -> PyText {
        const auto& n = e.nodes[i];
        const auto arg = [&](std::size_t k) { return emit(n.args[k]); };
        switch (n.op) {
        case Op::number: return {n.value == std::numbers::pi ? std::string{"np.pi"} : number(n.value)};
        case Op::name: {
            if (!n.name.starts_with("_v")) return ok = false, PyText{};
            const auto k = static_cast<std::size_t>(std::stoul(n.name.substr(2)));
            return {py_var(names.at(k))};
        }
        case Op::neg: return {"-" + wrap(arg(0), 7), 7};
        case Op::plus: return {"+" + wrap(arg(0), 7), 7};
        case Op::not_: return {"float(" + wrap(arg(0), 5) + " == 0)"};
        case Op::add: return {wrap(arg(0), 5) + " + " + wrap(arg(1), 6), 5};
        case Op::sub: return {wrap(arg(0), 5) + " - " + wrap(arg(1), 6), 5};
        case Op::mul: return {wrap(arg(0), 6) + " * " + wrap(arg(1), 7), 6};
        case Op::div: return {wrap(arg(0), 6) + " / " + wrap(arg(1), 7), 6};
        case Op::pow: return {wrap(arg(0), 9) + " ** " + wrap(arg(1), 7), 8};
        case Op::lt: return {"float(" + wrap(arg(0), 5) + " < " + wrap(arg(1), 5) + ")"};
        case Op::le: return {"float(" + wrap(arg(0), 5) + " <= " + wrap(arg(1), 5) + ")"};
        case Op::gt: return {"float(" + wrap(arg(0), 5) + " > " + wrap(arg(1), 5) + ")"};
        case Op::ge: return {"float(" + wrap(arg(0), 5) + " >= " + wrap(arg(1), 5) + ")"};
        case Op::eq: return {"float(" + wrap(arg(0), 5) + " == " + wrap(arg(1), 5) + ")"};
        case Op::ne: return {"float(" + wrap(arg(0), 5) + " != " + wrap(arg(1), 5) + ")"};
        case Op::and_: return {"float(" + wrap(arg(0), 5) + " != 0 and " + wrap(arg(1), 5) + " != 0)"};
        case Op::or_: return {"float(" + wrap(arg(0), 5) + " != 0 or " + wrap(arg(1), 5) + " != 0)"};
        case Op::call: {
            std::string args;
            for (std::size_t k = 0; k < n.args.size(); ++k) args += (k ? ", " : "") + arg(k).text;
            const auto fn = n.name == "sqrt" ? "np.sqrt" : n.name == "log10" ? "np.log10" : n.name;
            return {fn + "(" + args + ")"};
        }
        }
        return ok = false, PyText{};
    };
    auto out = emit(e.root);
    if (!ok) return std::nullopt;
    return std::move(out.text);
}

std::string Emitter::text_expression(std::string_view raw) const {
    std::string body;
    for (std::size_t at = 0; at < raw.size(); ++at) {
        const char c = raw[at];
        if (c == '$' && at + 1 < raw.size() && raw[at + 1] == '$') {
            body += '$';
            ++at;
        } else if (c == '$') {
            auto end = at + 1;
            while (end < raw.size() && (std::isalnum(static_cast<unsigned char>(raw[end])) || raw[end] == '_')) ++end;
            body += "{" + py_var(raw.substr(at + 1, end - at - 1)) + "}";
            at = end - 1;
        } else if (c == '{' || c == '}') {
            body += std::string(2, c);
        } else {
            body += c;
        }
    }
    return "f" + py_str(body);
}

std::string Emitter::expr(const NodeIo& io, std::string_view param) const {
    using Kind = pipeline::ParamSpec::Kind;
    const auto& n = io.node;
    const auto spec = std::ranges::find_if(n.type->params, [&](const pipeline::ParamSpec& p) { return p.name == param; });
    const auto kind = spec == n.type->params.end() ? Kind::text : spec->kind;
    const auto value = n.params.count(std::string{param}) ? n.params.at(std::string{param}) : spec == n.type->params.end() ? std::string{} : spec->fallback;
    const bool numeric = kind == Kind::integer || kind == Kind::real;
    if (const auto raw = n.source.find(std::string{param}); raw != n.source.end()) {
        const auto names = pipeline::variables_in(raw->second);
        if (!names.empty()) {
            const bool whole = names.size() == 1 && raw->second == "$" + names.front();
            if (numeric)
                if (auto e = py_expression(raw->second)) return kind == Kind::integer && !(whole && int_vars_.contains(names.front())) ? "int(" + *e + ")" : *e;
            return whole ? py_var(names.front()) : text_expression(raw->second);
        }
    }
    if (numeric)
        if (const auto v = core::eval_number(value)) return kind == Kind::integer ? fmt::format("{}", static_cast<long long>(std::llround(*v))) : number(*v);
    return py_str(value);
}

bool uses_variable(const NodeIo& io, std::string_view param) {
    const auto raw = io.node.source.find(std::string{param});
    return raw != io.node.source.end() && !pipeline::variables_in(raw->second).empty();
}

std::string flag(const NodeIo& io, Emitter& em, std::string_view param, std::string_view yes) {
    if (uses_variable(io, param)) return "(" + em.expr(io, param) + " == " + py_str(yes) + ")";
    const auto it = io.node.params.find(std::string{param});
    return it != io.node.params.end() && it->second == yes ? "True" : "False";
}

std::string samples_expr(const NodeIo& io, Emitter& em, std::string_view param, const std::string& fs) {
    if (uses_variable(io, param)) {
        em.function("as_samples");
        return "as_samples(" + em.expr(io, param) + ", " + fs + ")";
    }
    const auto& value = pipeline::param_text(io.node.params, param);
    if (value.ends_with('s')) {
        em.function("as_samples");
        return "as_samples(" + py_str(value) + ", " + fs + ")";
    }
    return std::to_string(pipeline::param_int(io.node.params, param));
}

std::string count_expr(const NodeIo& io, Emitter& em, std::string_view param) {
    const auto& params = io.node.type->params;
    const auto spec = std::ranges::find_if(params, [&](const pipeline::ParamSpec& s) { return s.name == param; });
    if (spec != params.end() && spec->kind == pipeline::ParamSpec::Kind::samples) return samples_expr(io, em, param, io.in_fs.empty() ? std::string{"fs"} : io.in_fs[0]);
    return em.expr(io, param);
}

std::string Emitter::numbers(std::string key, std::span<const double> v) {
    if (v.size() > 64) return data(std::move(key), io::np_vector({v.begin(), v.end()}));
    std::string out = "[";
    for (std::size_t k = 0; k < v.size(); ++k) out += (k ? ", " : "") + number(v[k]);
    return out + "]";
}

std::string Emitter::file(const std::filesystem::path& absolute) const {
    if (preview_ || out_dir_.empty()) return "Path(" + py_str(std::string{reinterpret_cast<const char*>(absolute.filename().u8string().c_str())}) + ")";
    std::error_code ec;
    const auto target = std::filesystem::weakly_canonical(absolute, ec);
    const auto base = std::filesystem::weakly_canonical(out_dir_, ec);
    auto rel = target.lexically_relative(base);
    std::size_t up = 0;
    for (const auto& part : rel) up += part == "..";
    if (up > 2) rel.clear();
    const auto text = [](const std::filesystem::path& p) {
        const auto u = p.generic_u8string();
        return std::string{u.begin(), u.end()};
    };
    if (rel.empty()) return "Path(" + py_str(text(target.empty() ? absolute : target)) + ")";
    return "HERE / " + py_str(text(rel));
}

void Emitter::keep_wavelet(std::string_view name) {
    const auto builtin = core::wavelet_names();
    const auto user = core::user_wavelet_names();
    if (std::ranges::find(builtin, name) != builtin.end() && std::ranges::find(user, name) == user.end() && !by_filter_bank().contains(name)) return;
    const auto bank = core::wavelet(name);
    if (!bank) return;
    for (const auto& [key, array] : pipeline::bank_entries(*bank))
        if (key != "name") put("wavelet." + std::string{name} + "." + key, array);
}

std::string Emitter::wavelet_of(const NodeIo& io, std::string_view param) {
    const auto value = io.node.params.count(std::string{param}) ? io.node.params.at(std::string{param}) : std::string{};
    if (!uses_variable(io, param)) return wavelet(value);
    need_wavelets(value);
    return "wavelet(" + expr(io, param) + ")";
}

void Emitter::need_wavelets(std::string_view current) {
    import_line("import pywt");
    function("wavelet");
    if (std::ranges::find(prelude_, "NO_WAVELET = " + py_str(tr("python.live.no_wavelet"))) == prelude_.end()) prelude_.push_back("NO_WAVELET = " + py_str(tr("python.live.no_wavelet")));
    if (preview_) return;
    keep_wavelet(current);
    for (const auto& name : by_filter_bank()) keep_wavelet(name);
}

std::string Emitter::wavelet(std::string_view name) {
    import_line("import pywt");
    const auto builtin = core::wavelet_names();
    const auto user = core::user_wavelet_names();
    const bool by_name = std::ranges::find(builtin, name) != builtin.end() && std::ranges::find(user, name) == user.end() && !by_filter_bank().contains(name);
    if (preview_ || by_name) return py_str(name);
    if (const auto it = wavelets_.find(name); it != wavelets_.end()) return it->second;
    const auto bank = core::wavelet(name);
    if (!bank) return py_str(name);
    const auto var = name_for("w_" + std::string{name});
    std::string list;
    for (const auto& [key, array] : pipeline::bank_entries(*bank)) {
        if (key == "name") continue;
        const auto full = "wavelet." + std::string{name} + "." + key;
        put(full, array);
        list += (list.empty() ? "" : ", ") + ("D[" + py_str(full) + "]");
    }
    prelude_.push_back(var + " = pywt.Wavelet(" + py_str(name) + ", filter_bank=[" + list + "])");
    wavelets_.emplace(std::string{name}, var);
    return var;
}

}  // namespace signals::python
