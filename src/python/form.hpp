#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "io/npy.hpp"
#include "pipeline/block.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/value.hpp"

namespace signals::python {

struct NodeIo {
    const pipeline::GraphNode& node;
    std::vector<std::string> in;
    std::vector<std::string> out;
    std::vector<std::string> in_fs;
    std::vector<const pipeline::Value*> in_values;
    std::vector<const pipeline::Value*> out_values;
    std::filesystem::path base_dir = {};
};

struct FunctionFile {
    std::string_view name, text;
};

std::span<const FunctionFile> python_functions();
const FunctionFile* find_python_function(std::string_view name);
std::vector<std::string> function_needs(std::string_view text);
std::vector<std::string> function_imports(std::string_view text);
const std::set<std::string, std::less<>>& by_filter_bank();

std::string py_name(std::string_view id, const std::set<std::string, std::less<>>& taken);
std::string py_str(std::string_view text);
std::string py_comment(std::string_view text);

class Emitter {
public:
    explicit Emitter(bool preview = false) : preview_{preview} {}

    void set_node(std::string id) { node_ = std::move(id); }
    std::string name_for(std::string_view id);
    std::string data(std::string key, io::NpArray value);
    void put(std::string key, io::NpArray value);
    void import_line(std::string_view line);
    void function(std::string_view name);
    std::string number(double v) const;
    std::string numbers(std::string key, std::span<const double> v);
    std::string wavelet(std::string_view name);
    void set_out_dir(std::filesystem::path dir) { out_dir_ = std::move(dir); }
    std::string file(const std::filesystem::path& absolute) const;

    bool preview() const { return preview_; }
    const std::vector<std::string>& imports() const { return imports_; }
    std::vector<std::string> functions() const;
    const std::vector<std::string>& missing() const { return missing_; }
    const std::vector<std::string>& prelude() const { return prelude_; }
    const io::NpzEntries& entries() const { return entries_; }

private:
    bool preview_;
    std::string node_;
    std::filesystem::path out_dir_;
    std::set<std::string, std::less<>> taken_;
    std::vector<std::string> imports_, functions_, missing_, prelude_;
    std::map<std::string, std::string, std::less<>> wavelets_;
    io::NpzEntries entries_;
};

struct PyForm {
    std::string block;
    double tol = 1e-12;
    std::function<core::Result<std::string>(const NodeIo&, Emitter&)> emit;
};

std::span<const PyForm> py_forms();
const PyForm* find_py_form(std::string_view block);
core::Error unsupported(const NodeIo& io, std::string_view param);
std::string python_preview(const pipeline::BlockType& t, const pipeline::Params& params);

void register_basic_forms(std::vector<PyForm>& out);
void register_filter_forms(std::vector<PyForm>& out);

}  // namespace signals::python
