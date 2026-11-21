#include "learn/migrate_course.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <toml++/toml.hpp>

#include "i18n/i18n.hpp"
#include "learn/task.hpp"
#include "pipeline/migrate.hpp"

namespace signals::learn {
namespace {

using pipeline::Rename;
using Kind = Rename::Kind;

struct Edit {
    std::size_t line = 0, column = 0;
    std::string from, to;
};

Edit edit_at(const toml::source_region& where, std::string from, std::string to) {
    Edit e;
    e.line = where.begin.line;
    e.column = where.begin.column;
    e.from = std::move(from);
    e.to = std::move(to);
    return e;
}

std::size_t byte_column(std::string_view line, std::size_t column) {
    std::size_t at = 0;
    for (std::size_t cp = 1; cp < column && at < line.size(); ++cp) {
        ++at;
        while (at < line.size() && (static_cast<unsigned char>(line[at]) & 0xC0) == 0x80) ++at;
    }
    return at;
}

bool apply(std::string& text, const Edit& e) {
    std::size_t start = 0;
    for (std::size_t l = 1; l < e.line; ++l) {
        start = text.find('\n', start);
        if (start == std::string::npos) return false;
        ++start;
    }
    const auto end = text.find('\n', start);
    const std::string_view line{text.data() + start, (end == std::string::npos ? text.size() : end) - start};
    const auto at = line.find(e.from, byte_column(line, e.column));
    if (at == std::string_view::npos) return false;
    text.replace(start + at, e.from.size(), e.to);
    return true;
}

std::string in_quotes(std::string_view s) { return fmt::format("\"{}\"", s); }

std::string block_of(const std::map<std::string, std::string, std::less<>>& node_types, std::string_view ref) {
    const auto node = ref.substr(0, ref.find('.'));
    const auto it = node_types.find(node);
    return it == node_types.end() ? std::string{} : it->second;
}

}  // namespace

std::map<std::string, std::string, std::less<>> node_types_of(std::string_view sig) {
    std::map<std::string, std::string, std::less<>> out;
    if (const auto loaded = pipeline::load_sig(sig))
        for (const auto& n : loaded->doc.nodes) out.emplace(n.id, n.type);
    return out;
}

std::vector<std::string> rename_task(Task& task, std::span<const Rename> renames) {
    std::vector<std::string> old;
    if (renames.empty()) return old;
    auto nodes = node_types_of(task.start_sig);
    nodes.merge(node_types_of(task.solution_sig));
    const auto block = [&](std::string& name) {
        if (auto to = pipeline::renamed(renames, Kind::block, name, name)) old.push_back(std::exchange(name, *to) + " → " + *to);
    };
    for (auto& m : task.measures) {
        if (!m.block.empty()) block(m.block);
        if (!m.field.empty())
            if (auto to = pipeline::renamed(renames, Kind::field, block_of(nodes, m.a), m.field)) old.push_back(std::exchange(m.field, *to) + " → " + *to);
        if (const auto dot = m.param.find('.'); dot != std::string::npos)
            if (auto to = pipeline::renamed(renames, Kind::param, block_of(nodes, m.param), m.param.substr(dot + 1))) {
                old.push_back(m.param.substr(dot + 1) + " → " + *to);
                m.param = m.param.substr(0, dot + 1) + *to;
            }
    }
    for (auto& g : task.goals) {
        if (g.through) block(g.through->block);
        for (auto& f : g.forbid) block(f);
        for (auto& r : g.require) block(r.first);
    }
    return old;
}

core::Result<std::vector<std::string>> migrate_task_toml(std::string& text, std::span<const Rename> renames,
                                                         const std::map<std::string, std::string, std::less<>>& node_types) {
    toml::table t;
    try {
        t = toml::parse(text);
    } catch (const toml::parse_error& e) {
        return core::make_error(core::Errc::invalid_argument, fmt::format("task.toml:{}: {}", e.source().begin.line, std::string{e.description()}));
    }
    std::vector<Edit> edits;
    std::vector<std::string> notes;
    const auto value = [&](const toml::node* n, const std::string& from, const std::string& to) {
        if (!n) return;
        edits.push_back(edit_at(n->source(), in_quotes(from), in_quotes(to)));
        notes.push_back(fmt::format(fmt::runtime(tr("learn.migrate.rename")), fmt::arg("line", n->source().begin.line), fmt::arg("from", from), fmt::arg("to", to)));
    };
    const auto block_value = [&](const toml::node* n) {
        if (!n || !n->is_string()) return;
        const auto old = n->value_or(std::string{});
        if (auto to = pipeline::renamed(renames, Kind::block, old, old)) value(n, old, *to);
    };
    if (const auto* measures = t["measure"].as_array())
        for (const auto& m : *measures) {
            const auto* table = m.as_table();
            if (!table) continue;
            block_value(table->get("block"));
            if (const auto* field = table->get("field"); field && field->is_string()) {
                const auto old = field->value_or(std::string{});
                const auto a = (*table)["a"].value_or(std::string{});
                if (auto to = pipeline::renamed(renames, Kind::field, block_of(node_types, a), old)) value(field, old, *to);
            }
            if (const auto* param = table->get("param"); param && param->is_string()) {
                const auto old = param->value_or(std::string{});
                const auto dot = old.find('.');
                if (dot != std::string::npos)
                    if (auto to = pipeline::renamed(renames, Kind::param, block_of(node_types, old), old.substr(dot + 1)))
                        value(param, old, old.substr(0, dot + 1) + *to);
            }
        }
    if (const auto* goals = t["goal"].as_array())
        for (const auto& g : *goals) {
            const auto* table = g.as_table();
            if (!table) continue;
            if (const auto* through = (*table)["through"].as_table()) block_value(through->get("block"));
            if (const auto* forbid = (*table)["forbid"].as_array())
                for (const auto& item : *forbid) block_value(&item);
            if (const auto* require = (*table)["require"].as_table())
                for (const auto& [key, v] : *require) {
                    const std::string old{key.str()};
                    if (auto to = pipeline::renamed(renames, Kind::block, old, old)) {
                        edits.push_back(edit_at(key.source(), old, *to));
                        notes.push_back(fmt::format(fmt::runtime(tr("learn.migrate.rename")), fmt::arg("line", key.source().begin.line), fmt::arg("from", old), fmt::arg("to", *to)));
                    }
                }
        }
    std::ranges::sort(edits, [](const Edit& a, const Edit& b) { return a.line != b.line ? a.line > b.line : a.column > b.column; });
    for (const auto& e : edits)
        if (!apply(text, e)) return core::make_error(core::Errc::invalid_argument, fmt::format("task.toml:{}: {}", e.line, e.from));
    return notes;
}

}  // namespace signals::learn
