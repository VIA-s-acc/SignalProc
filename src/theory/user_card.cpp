#include "theory/user_card.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"
#include "pipeline/flatten.hpp"
#include "theory/pipeline_cards.hpp"
#include "theory/sections.hpp"

namespace signals::theory {

namespace {

std::string port_lines(const std::vector<pipeline::PortSpec>& ports, const std::string& word) {
    std::string out;
    for (const auto& p : ports)
        out += "- " + word + " `" + p.name + "` — " + std::string{pipeline::to_string(p.type)} + (p.optional ? ", " + tr("docs.blocks.optional") : "") + "\n";
    return out;
}

std::string link_line(const std::string& type, const pipeline::PipelineDoc& doc) {
    if (pipeline::find_own_block(doc, type)) return "- `" + type + "` — " + tr("theory.user_card.own");
    const auto target = block_theory(type);
    if (target.section) {
        if (const auto* s = find_section(target.id)) return "- `" + type + "` — §" + s->number + " " + s->title;
    } else if (const auto* c = find_card(target.id)) {
        return "- `" + type + "` — " + c->title;
    }
    return "- `" + type + "`";
}

}  // namespace

Card user_block_card(const pipeline::BlockDef& def, const pipeline::PipelineDoc& doc) {
    Card card;
    card.id = "block:" + def.name;
    card.title = def.name;
    const auto origin = def.origin.generic_u8string();
    card.source.assign(origin.begin(), origin.end());
    std::string about;
    for (const auto& line : def.comments.above) {
        std::string_view text = line;
        if (text.starts_with('#')) text.remove_prefix(1);
        if (text.starts_with(' ')) text.remove_prefix(1);
        about += std::string{text} + "\n";
    }
    card.sections.push_back({tr("theory.card.purpose"), about.empty() ? tr("theory.user_card.no_about") : about});
    std::string params;
    for (const auto& key : def.order) params += "- `" + key + "` = `" + def.defaults.at(key) + "`\n";
    card.sections.push_back({tr("theory.user_card.params"), params.empty() ? "—" : params});
    const auto types = pipeline::own_block_types(doc);
    if (const auto t = std::ranges::find(types, def.name, &pipeline::BlockType::id); t != types.end())
        card.sections.push_back({tr("theory.user_card.ports"), port_lines(t->inputs, tr("theory.user_card.in")) + port_lines(t->outputs, tr("theory.user_card.out"))});
    card.sections.push_back({tr("theory.user_card.graph"), "```\n" + pipeline::block_text(def, doc) + "```\n"});
    std::set<std::string, std::less<>> seen;
    std::string links;
    for (const auto& n : def.nodes)
        if (seen.insert(n.type).second) links += link_line(n.type, doc) + "\n";
    card.sections.push_back({tr("theory.card.links"), links.empty() ? "—" : links});
    return card;
}

}  // namespace signals::theory
