#include <vector>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "python/form.hpp"

namespace signals::python {
namespace {

std::vector<PyForm> build() {
    std::vector<PyForm> out;
    register_basic_forms(out);
    register_filter_forms(out);
    register_wavelet_forms(out);
    register_effect_forms(out);
    register_analysis_forms(out);
    return out;
}

}  // namespace

std::span<const PyForm> py_forms() {
    static const std::vector<PyForm> all = build();
    return all;
}

const PyForm* find_py_form(std::string_view block) {
    for (const auto& f : py_forms())
        if (f.block == block) return &f;
    return nullptr;
}

core::Error unsupported(const NodeIo& io, std::string_view param) {
    const std::string block = io.node.type ? io.node.type->id : std::string{"?"};
    if (param.empty())
        return {core::Errc::unsupported, fmt::format(fmt::runtime(tr("python.unsupported_block")), fmt::arg("node", io.node.id), fmt::arg("block", block))};
    const auto it = io.node.params.find(param);
    const std::string value = it == io.node.params.end() ? std::string{} : it->second;
    return {core::Errc::unsupported, fmt::format(fmt::runtime(tr("python.unsupported")), fmt::arg("node", io.node.id), fmt::arg("block", block),
                                                 fmt::arg("param", param), fmt::arg("value", value))};
}

std::string python_preview(const pipeline::BlockType& t, const pipeline::Params& params) {
    const auto* form = find_py_form(t.id);
    if (!form) return {};
    const auto resolved = pipeline::resolve_params(t, params);
    if (!resolved) return {};
    pipeline::GraphNode node;
    node.id = "y";
    node.type = &t;
    node.params = *resolved;
    std::vector<std::string> in, fs, out;
    for (const auto& p : t.inputs) {
        in.push_back(py_name(p.name == "in" ? std::string_view{"x"} : std::string_view{p.name}, {}));
        fs.emplace_back("fs");
    }
    if (t.outputs.size() == 1) out.emplace_back("y");
    else
        for (const auto& p : t.outputs) out.push_back("y_" + p.name);
    const NodeIo io{node, in, out, fs, std::vector<const pipeline::Value*>(in.size(), nullptr), std::vector<const pipeline::Value*>(out.size(), nullptr)};
    Emitter em{true};
    em.set_node("y");
    const auto code = form->emit(io, em);
    if (!code) return {};
    return code->substr(0, code->find('\n'));
}

}  // namespace signals::python
