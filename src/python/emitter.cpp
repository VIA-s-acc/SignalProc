#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <fmt/format.h>

#include "core/design/families.hpp"
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
    "sys", "math", "wavfile", "D", "HERE", "verify", "verify_text", "verify_name", "report", "plt", "Audio"};

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

const std::set<std::string, std::less<>>& by_filter_bank() {
    static const std::set<std::string, std::less<>> names{"sym2", "sym3", "sym4", "sym5", "sym6", "sym7", "sym8", "sym9", "sym10", "sym12", "sym13", "sym17"};
    return names;
}

std::string py_name(std::string_view id, const std::set<std::string, std::less<>>& taken) {
    std::string name{id};
    while (kReserved.contains(name) || taken.contains(name) || find_python_function(name)) name += '_';
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
    auto name = py_name(id, taken_);
    taken_.insert(name);
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

std::string Emitter::numbers(std::string key, std::span<const double> v) {
    if (v.size() > 64) return data(std::move(key), io::np_vector({v.begin(), v.end()}));
    std::string out = "[";
    for (std::size_t k = 0; k < v.size(); ++k) out += (k ? ", " : "") + number(v[k]);
    return out + "]";
}

std::string Emitter::file(const std::filesystem::path& absolute) const {
    if (preview_ || out_dir_.empty()) return "Path(" + py_str(std::string{reinterpret_cast<const char*>(absolute.filename().u8string().c_str())}) + ")";
    const auto rel = absolute.lexically_relative(out_dir_);
    const auto text = [](const std::filesystem::path& p) {
        const auto u = p.generic_u8string();
        return std::string{u.begin(), u.end()};
    };
    if (rel.empty()) return "Path(" + py_str(text(absolute)) + ")";
    return "HERE / " + py_str(text(rel));
}

std::string Emitter::wavelet(std::string_view name) {
    import_line("import pywt");
    if (preview_ || !by_filter_bank().contains(name)) return py_str(name);
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
