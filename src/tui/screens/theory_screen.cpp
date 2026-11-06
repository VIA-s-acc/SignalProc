#include "tui/screens/theory_screen.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <cstdlib>
#include <utility>

#include <fmt/format.h>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>

#include "i18n/i18n.hpp"
#include "theory/blocks.hpp"
#include "theory/cards.hpp"
#include "theory/sections.hpp"
#include "theory/text.hpp"
#include "tui/shell/palette.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/screen_util.hpp"

namespace signals::tui {
namespace {

constexpr int kContentsWidth = 32;

std::string clip(const std::string& s, int width, bool ascii) {
    if (width <= 0) return {};
    if (static_cast<int>(theory::display_width(s)) <= width) return s;
    return cut_columns(s, 0, width - 1) + (ascii ? "~" : "…");
}

std::string entry_label(const theory::Section& s) { return s.number.empty() ? s.title : s.number + "  " + s.title; }

const theory::Card* card_for_section(std::string_view section) {
    for (const auto& b : theory::block_catalog())
        if (b.section == section)
            if (const auto* c = theory::find_card(b.id)) return c;
    return nullptr;
}

const std::vector<bool>& cards_below() {
    static const auto marks = [] {
        const auto& all = theory::sections();
        std::vector<bool> out(all.size(), false);
        for (std::size_t k = 0; k < all.size(); ++k)
            if (card_for_section(all[k].id))
                for (auto p = all[k].parent; p; p = all[*p].parent) out[*p] = true;
        return out;
    }();
    return marks;
}

struct Window {
    std::size_t first = 0, shown = 0;
};

Window place_window(std::size_t& first, std::size_t n, std::size_t lines, std::optional<std::size_t> follow) {
    const auto fit = [&](std::size_t f) {
        std::size_t r = std::max<std::size_t>(1, lines);
        if (f > 0 && r > 1) --r;
        if (f + r < n && r > 1) --r;
        return r;
    };
    if (follow && *follow < n) {
        if (*follow < first) first = *follow;
        while (*follow >= first + fit(first)) ++first;
    }
    std::size_t last = 0;
    while (last + fit(last) < n) ++last;
    first = std::min(first, last);
    return {first, std::min(fit(first), n - first)};
}

std::vector<const theory::Card*> catalog_cards() {
    std::vector<const theory::Card*> out;
    for (const auto& b : theory::block_catalog())
        if (const auto* c = theory::find_card(b.id)) out.push_back(c);
    return out;
}

void wheel_window(std::size_t& first, bool down) {
    if (down) ++first;
    else if (first > 0) --first;
}

}  // namespace

TheoryScreen::TheoryScreen(UiContext& ui, std::shared_ptr<TheoryNavigator> nav, Mode mode)
    : ui_{ui}, nav_{std::move(nav)}, mode_{mode},
      glyphs_{ui.caps.glyphs == GlyphMode::ascii ? theory::MathGlyphs::ascii : theory::MathGlyphs::unicode} {}

std::string TheoryScreen::tab_label() const { return tr(mode_ == Mode::tutorial ? "tui.sub.tutorial" : "tui.sub.under_the_hood"); }

std::vector<Hint> TheoryScreen::hints() const {
    if (contents_keys()) {
        std::vector<Hint> out{{"↑↓", tr("tui.theory.hint.cursor")},          {"Enter", tr("tui.theory.hint.open")},
                              {"PgUp/PgDn", tr("tui.hint.page")},            {"Home/End", tr("tui.hint.ends")},
                              {"Esc  t", tr("tui.theory.hint.to_text")},     {"/", tr("tui.theory.hint.search")},
                              {tr("tui.signal.key.wheel"), tr("tui.hint.wheel_list")},
                              {tr("tui.signal.key.click"), tr("tui.theory.hint.open")}};
        if (breakpoint_ > Breakpoint::small) out.insert(out.begin() + 5, {"z", tr("tui.signal.hint.zoom")});
        return out;
    }
    std::vector<Hint> out{{"/", tr("tui.theory.hint.search")},          {"Tab", tr("tui.theory.hint.links")},
                          {"Enter", tr("tui.theory.hint.follow")},      {"b/f  Backspace", tr("tui.theory.hint.history")},
                          {"↑↓", tr("tui.theory.hint.scroll")},         {"PgUp/PgDn", tr("tui.hint.page")},
                          {"Home/End", tr("tui.hint.ends")},            {"←→", tr("tui.theory.hint.formulas")},
                          {"g", tr("tui.theory.hint.glyphs")},          {tr("tui.signal.key.wheel"), tr("tui.theory.hint.scroll")},
                          {tr("tui.signal.key.click"), tr("tui.theory.hint.click")}};
    if (mode_ == Mode::under_the_hood) {
        out.push_back({"n", tr("tui.theory.hint.numbers")});
        out.push_back({"o", tr("tui.theory.hint.source")});
        out.push_back({"Esc", tr("tui.theory.hint.back")});
    }
    std::vector<Hint> first{{"t", tr("tui.theory.hint.contents")}};
    if (breakpoint_ > Breakpoint::small) first.push_back({"z", tr("tui.signal.hint.zoom")});
    if (mode_ == Mode::tutorial && card_for_section(theory::sections()[current_section()].id)) first.push_back({"i", tr("tui.theory.hint.card")});
    out.insert(out.begin(), first.begin(), first.end());
    return out;
}

std::string TheoryScreen::context() const {
    const auto& s = theory::sections()[current_section()];
    return "{#" + s.id + "}";
}

const TheoryPage* TheoryScreen::page() const { return nav_->last(TheoryLink::Kind::section); }

std::size_t TheoryScreen::current_section() const {
    if (mode_ == Mode::under_the_hood) {
        if (const auto* p = nav_->last(TheoryLink::Kind::card))
            if (const auto* b = theory::find_block(p->link.id))
                if (const auto* s = theory::find_section(b->section)) return theory::index_of(*s);
        return 0;
    }
    if (const auto* p = page())
        if (const auto* s = theory::find_section(p->link.id)) return theory::index_of(*s);
    return 0;
}

void TheoryScreen::open_section(std::string_view id) {
    if (!theory::find_section(id)) return;
    nav_->open({{TheoryLink::Kind::section, std::string{id}}, nullptr, {}, {}, 0});
}

bool TheoryScreen::open_section_card() {
    const auto* card = card_for_section(theory::sections()[current_section()].id);
    if (!card) return false;
    nav_->open({{TheoryLink::Kind::card, card->id}, std::make_shared<theory::LiveValues>(), {}, "tutorial", 0});
    if (ui_.navigate) ui_.navigate("under_the_hood", {});
    return true;
}

void TheoryScreen::follow(const std::string& target) {
    if (target.starts_with("#")) {
        open_section(target.substr(1));
        if (mode_ == Mode::under_the_hood && ui_.navigate) ui_.navigate("tutorial", {});
    } else if (target.starts_with("card:")) {
        nav_->open({{TheoryLink::Kind::card, target.substr(5)}, std::make_shared<theory::LiveValues>(), {}, {}, 0});
    } else if (target.starts_with("view:")) {
        if (ui_.navigate) ui_.navigate(target.substr(5), view_args(card_page()));
    } else if (target.starts_with("file://")) {
        open_source();
    }
}

TheoryPage* TheoryScreen::card_page() { return nav_->last(TheoryLink::Kind::card); }

void TheoryScreen::clamp_scroll() { scroll_ = std::clamp(scroll_, 0, std::max(0, doc_.height - visible_rows_)); }

void TheoryScreen::refresh_values(TheoryPage& p) {
    if (!p.values) p.values = std::make_shared<theory::LiveValues>();
    if (!p.publisher) return;
    theory::LiveValues fresh;
    p.publisher(fresh);
    if (fresh.snapshot) {
        p.values->snapshot = true;
        return;
    }
    *p.values = std::move(fresh);
}

NavArgs TheoryScreen::view_args(const TheoryPage* p) const {
    NavArgs args;
    if (!p || !p->values) return args;
    if (const auto* w = p->values->find("block.wavelet")) args.wavelet = w->text;
    const bool matrix_card = p->link.id == "bank_matrix" || p->link.id == "toeplitz";
    if (const auto* n = p->values->find("block.n"); n && matrix_card) {
        std::string digits;
        for (char ch : n->text)
            if (ch >= '0' && ch <= '9') digits += ch;
        if (!digits.empty() && digits.size() < 10) args.n = static_cast<std::size_t>(std::stoul(digits));
    }
    return args;
}

void TheoryScreen::open_source() {
    const auto* p = nav_->last(TheoryLink::Kind::card);
    const auto* card = p ? theory::find_card(p->link.id) : nullptr;
    if (!card) return;
    const auto path = ui_.repo_root / card->source;
    std::error_code ec;
    if (ui_.repo_root.empty() || !std::filesystem::exists(path, ec)) {
        notice_ = fmt::format(fmt::runtime(tr("tui.theory.no_sources")), fmt::arg("path", path.string()));
        return;
    }
    notice_.clear();
    if (ui_.open_in_editor) ui_.open_in_editor(path);
}

std::string TheoryScreen::card_markdown(const theory::Card& card, bool live) const {
    std::string md;
    const auto numbers = tr("theory.card.numbers");
    for (const auto& s : card.sections) {
        md += "## " + s.heading + "\n\n";
        if (fold_numbers_ && s.heading == numbers) {
            md += "*" + tr("tui.theory.folded") + "*\n\n";
            continue;
        }
        if (!live && s.heading == numbers) md += "*" + tr("tui.theory.no_values") + "*\n\n";
        md += s.body + "\n";
        if (s.heading == tr("theory.card.links")) {
            std::string buttons;
            for (const auto& v : card.views) {
                const auto key = "tui.theory.view." + v;
                buttons += std::string{buttons.empty() ? "" : " · "} + "[" + tr(key) + "](view:" + v + ")";
            }
            buttons += std::string{buttons.empty() ? "" : " · "} + "*" + tr("tui.theory.view.pipeline_later") + "*";
            md += buttons + "\n\n";
        }
        if (s.heading == tr("theory.card.implementation")) {
            const auto url = "file://" + (ui_.repo_root / card.source).string();
            md += fmt::format(fmt::runtime(tr("tui.theory.footer")), fmt::arg("source", "[" + card.source + "](" + url + ")"),
                              fmt::arg("function", card.function), fmt::arg("test", card.test)) +
                  "\n\n";
        }
    }
    return md;
}

ftxui::Element TheoryScreen::origin_line(const TheoryPage* p, int width) const {
    using namespace ftxui;
    const auto& th = ui_.theme;
    std::string source = tr("tui.theory.from_book");
    if (p && p->values && p->values->snapshot) source = tr("tui.theory.snapshot") + (p->values->source.empty() ? "" : " · " + p->values->source);
    else if (p && p->values && !p->values->source.empty()) source = p->values->source + " " + tr("tui.theory.by_f1");
    const bool ascii = glyphs_ == theory::MathGlyphs::ascii;
    const auto depth = ui_.caps.color == ColorDepth::truecolor ? "tui.theory.depth.true"
                       : ui_.caps.color == ColorDepth::palette256 ? "tui.theory.depth.256"
                       : ui_.caps.color == ColorDepth::ansi16      ? "tui.theory.depth.16"
                                                                   : "tui.theory.depth.none";
    const auto terminal = fmt::format(fmt::runtime(tr("tui.theory.terminal")), fmt::arg("emulator", ui_.caps.emulator.empty() ? tr("tui.theory.emulator") : ui_.caps.emulator),
                                      fmt::arg("depth", tr(depth)));
    const auto opened = fmt::format(fmt::runtime(tr("tui.theory.opened_from")), fmt::arg("source", source));
    const std::string formulas = " · " + tr("tui.theory.formulas") + " " + (ascii ? "Unicode 2-D [ASCII]" : "[Unicode 2-D] ASCII");
    const std::string history = " · " + tr("tui.theory.history");
    const std::string term = " · " + terminal;
    const auto w = [](const std::string& s) { return static_cast<int>(theory::display_width(s)); };
    int used = std::min(w(opened), width);
    Elements parts{text(clip(opened, width, ui_.caps.glyphs == GlyphMode::ascii)) | fg(th.fg, ui_)};
    const bool show_formulas = used + w(formulas) <= width;
    if (show_formulas) used += w(formulas);
    const bool show_terminal = used + w(term) <= width;
    if (show_terminal) used += w(term);
    const bool show_history = used + w(history) <= width;
    if (show_history) parts.push_back(text(history) | fg(th.dim, ui_));
    if (show_formulas) {
        parts.push_back(text(" · " + tr("tui.theory.formulas") + " ") | fg(th.dim, ui_));
        parts.push_back(text(ascii ? "Unicode 2-D " : "[Unicode 2-D] ") | fg(ascii ? th.dim : th.accent, ui_));
        parts.push_back(text(ascii ? "[ASCII]" : "ASCII") | fg(ascii ? th.accent : th.dim, ui_));
    }
    if (show_terminal) parts.push_back(text(term) | fg(th.dim, ui_));
    return hbox(std::move(parts));
}

ftxui::Element TheoryScreen::more_row(bool up, std::size_t count, int width, ftxui::Box& box) const {
    using namespace ftxui;
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const std::string arrow = ascii ? (up ? "^ " : "v ") : (up ? "▲ " : "▼ ");
    return text(clip(arrow + fmt::format(fmt::runtime(tr("tui.theory.blocks_more")), fmt::arg("count", count)), width - 2, ascii)) | fg(ui_.theme.dim, ui_) |
           reflect(box);
}

ftxui::Element TheoryScreen::blocks_list(const theory::Card* current, int width, int rows, std::optional<std::size_t> cursor) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const auto all = catalog_cards();
    const std::size_t n = all.size();
    std::optional<std::size_t> follow;
    if (cursor && toc_follow_cursor_) {
        follow = *cursor;
        toc_follow_cursor_ = false;
    }
    if (current && current->id != blocks_followed_) {
        blocks_followed_ = current->id;
        for (std::size_t i = 0; i < n; ++i)
            if (all[i] == current) follow = i;
    }
    const auto [first, shown] = place_window(blocks_first_, n, static_cast<std::size_t>(std::max(1, rows - 1)), follow);
    blocks_page_ = std::max<std::size_t>(1, shown);

    const bool clipped = shown < n;
    Elements out{text(clipped ? fmt::format(fmt::runtime(tr("tui.theory.blocks_range")), fmt::arg("from", first + 1), fmt::arg("to", first + shown),
                                            fmt::arg("count", n))
                              : tr("tui.theory.blocks")) |
                 fg(th.dim, ui_)};
    if (first > 0) out.push_back(more_row(true, first, width, blocks_up_box_));
    block_boxes_.clear();
    block_boxes_.reserve(shown);
    for (std::size_t i = first; i < first + shown; ++i) {
        const bool on = all[i] == current;
        auto label = clip(all[i]->title, width - 2, ascii);
        auto row = hbox({text(label), filler()});
        if (cursor && *cursor == i) row = row | fg(th.bg, ui_) | bg(th.fg, ui_);
        else row = on ? row | bold | fg(th.bg, ui_) | bg(th.accent, ui_) : row | fg(th.accent, ui_);
        block_boxes_.emplace_back(ftxui::Box{}, all[i]->id);
        out.push_back(row | reflect(block_boxes_.back().first));
    }
    if (first + shown < n) out.push_back(more_row(false, n - first - shown, width, blocks_down_box_));
    return vbox(std::move(out)) | reflect(blocks_box_);
}

ftxui::Element TheoryScreen::card_panel(const TheoryPage* p, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto* card = p ? theory::find_card(p->link.id) : nullptr;
    if (!card) return panel(tr("tui.sub.under_the_hood"), {}, paragraph(tr("tui.theory.pick_block")) | fg(th.dim, ui_), true, ui_);
    const auto& s = theory::sections()[current_section()];
    auto title = (s.number.empty() ? std::string{} : s.number + " ") + card->title + "  {#" + s.id + "}";
    if (!s.tier.empty()) title += "  [" + s.tier + "]";
    const int inner = std::max(10, width - 4);
    visible_rows_ = std::max(1, height - 2 - (notice_.empty() ? 0 : 1));
    card_blocks_ = theory::parse_markdown(card_markdown(*card, p->values && !p->values->empty()));
    clamp_scroll();
    DocumentSpec spec{card_blocks_, inner, scroll_, hscroll_, glyphs_, focused_link_, p->values.get(), "─", visible_rows_};
    Elements body{document(spec, ui_, &doc_) | reflect(doc_box_) | flex};
    if (!notice_.empty()) body.push_back(text(notice_) | fg(th.warn, ui_));
    clamp_scroll();
    return panel(title, {{tr("tui.theory.chip.links"), false}}, vbox(std::move(body)), !searching_ && !contents_keys(), ui_);
}

void TheoryScreen::sync_version() {
    if (nav_->version() == seen_version_) return;
    seen_version_ = nav_->version();
    const auto* p = page();
    scroll_ = p ? p->scroll : 0;
    hscroll_ = 0;
    focused_link_.reset();
}

const std::vector<theory::Block>& TheoryScreen::blocks_for(std::size_t index) {
    if (auto it = cache_.find(index); it != cache_.end()) return it->second;
    const auto& all = theory::sections();
    const auto& s = all[index];
    std::string md{theory::own_body(s)};
    const auto kids = theory::children_of(index);
    if (!kids.empty()) {
        md += "\n\n";
        for (auto k : kids) md += "- {#" + all[k].id + "} " + (all[k].number.empty() ? std::string{} : all[k].title) + "\n";
    }
    std::string nav;
    if (index > 0 && !(s.parent && *s.parent == index - 1)) nav += "← {#" + all[index - 1].id + "}";
    if (s.parent) nav += std::string{nav.empty() ? "" : " · "} + "↑ {#" + all[*s.parent].id + "}";
    if (index + 1 < all.size()) nav += std::string{nav.empty() ? "" : " · "} + "{#" + all[index + 1].id + "} →";
    if (!nav.empty()) md += "\n\n---\n\n" + nav + "\n";
    return cache_.emplace(index, theory::parse_markdown(md)).first->second;
}

std::vector<TheoryScreen::Entry> TheoryScreen::contents(std::size_t current) const {
    const auto& all = theory::sections();
    std::size_t chapter = current;
    while (all[chapter].level > 2 && all[chapter].parent) chapter = *all[chapter].parent;
    std::vector<Entry> out;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].level != 2 || all[i].id == "references") continue;
        out.push_back({i, 0});
        if (i == chapter)
            for (auto k : theory::children_of(i)) out.push_back({k, 1});
    }
    return out;
}

ftxui::Element TheoryScreen::contents_panel(std::size_t current, int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto& all = theory::sections();
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    const int inner = std::max(4, width - 4);
    Elements rows;
    const auto total = all.size();
    const auto found = query_.empty() ? total : hits_.size();
    const auto count = fmt::format(fmt::runtime(tr("tui.theory.found")), fmt::arg("found", found), fmt::arg("total", total));
    if (query_.empty() && !searching_) {
        rows.push_back(hbox({text(tr("tui.theory.search_hint")) | fg(th.dim, ui_), filler(), text(count) | fg(th.dim, ui_)}));
    } else {
        const auto line = clip("/ " + query_ + (searching_ ? std::string{ascii ? "_" : "▏"} : std::string{}), inner - static_cast<int>(theory::display_width(count)) - 1, ascii);
        rows.push_back(hbox({text(line) | fg(searching_ ? th.accent : th.fg, ui_), filler(), text(count) | fg(th.dim, ui_)}));
    }
    rows.push_back(text(std::string{}) | size(HEIGHT, EQUAL, 1));
    std::vector<Entry> entries;
    std::size_t selected = 0;
    if (!query_.empty()) {
        for (auto h : hits_) entries.push_back({h, 0});
        selected = selected_hit_;
    } else {
        entries = contents(current);
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].section == current) selected = i;
    }
    const int block_rows = mode_ == Mode::under_the_hood ? std::max(4, height / 3) : 0;
    const int room = std::max(1, height - 2 - 2 - 3 - block_rows);
    std::optional<std::size_t> follow, cursor;
    const auto blocks = mode_ == Mode::under_the_hood ? catalog_cards().size() : 0;
    if (contents_keys() && entries.size() + blocks > 0) cursor = std::min(toc_cursor_, entries.size() + blocks - 1);
    const auto key = query_ + "\x1f" + (entries.empty() ? std::string{} : all[entries[std::min(selected, entries.size() - 1)].section].id);
    if (key != toc_followed_) {
        toc_followed_ = key;
        follow = selected;
    }
    if (cursor && *cursor < entries.size() && toc_follow_cursor_) {
        follow = *cursor;
        toc_follow_cursor_ = false;
    }
    const auto [first, shown] = place_window(toc_first_, entries.size(), static_cast<std::size_t>(room), follow);
    toc_page_ = std::max<std::size_t>(1, shown);
    toc_boxes_.clear();
    toc_boxes_.reserve(shown);
    Elements list;
    if (first > 0) list.push_back(more_row(true, first, inner + 2, toc_up_box_));
    for (std::size_t i = first; i < first + shown; ++i) {
        const auto& s = all[entries[i].section];
        const bool on = query_.empty() ? entries[i].section == current : i == selected;
        const std::string arrow = on && query_.empty() ? (ascii ? " <" : " ◀") : "";
        const bool own = card_for_section(s.id) != nullptr, below = !own && cards_below()[entries[i].section];
        const std::string info = own || below ? (ascii ? " [i]" : " ◈") : "";
        auto label = std::string(static_cast<std::size_t>(2 * entries[i].depth), ' ') + entry_label(s);
        label = clip(label, inner - static_cast<int>(theory::display_width(info + arrow)), ascii);
        const bool at_cursor = cursor && *cursor == i, plain = on || at_cursor;
        auto row = hbox({text(label) | (plain ? nothing : fg(th.fg, ui_)), text(info) | (plain ? nothing : fg(below ? th.dim : th.fg, ui_)), text(arrow),
                         filler()});
        if (at_cursor) row = row | fg(th.bg, ui_) | bg(th.fg, ui_);
        else if (on) row = row | bold | fg(th.bg, ui_) | bg(th.accent, ui_);
        toc_boxes_.emplace_back(ftxui::Box{}, entries[i].section);
        list.push_back(row | reflect(toc_boxes_.back().first));
    }
    if (first + shown < entries.size()) list.push_back(more_row(false, entries.size() - first - shown, inner + 2, toc_down_box_));
    rows.push_back(vbox(std::move(list)) | reflect(toc_box_));
    if (entries.empty()) rows.push_back(text(tr("tui.help.nothing")) | fg(th.dim, ui_));
    rows.push_back(filler());
    std::string rule;
    for (int i = 0; i < inner; ++i) rule += glyph(Glyph::h, ui_.caps.glyphs);
    rows.push_back(text(rule) | fg(th.dim, ui_));
    if (mode_ == Mode::under_the_hood) {
        const auto* p = nav_->last(TheoryLink::Kind::card);
        const auto block_cursor = cursor && *cursor >= entries.size() ? std::optional{*cursor - entries.size()} : std::nullopt;
        rows.push_back(blocks_list(p ? theory::find_card(p->link.id) : nullptr, inner, block_rows + 1, block_cursor));
    } else {
        rows.push_back(text(tr("tui.theory.references")) | fg(th.accent, ui_) | reflect(references_box_));
    }
    const auto title = mode_ == Mode::tutorial ? fmt::format(fmt::runtime(tr("tui.theory.contents")), fmt::arg("count", total))
                                               : tr("tui.theory.contents_short");
    return panel(title, {}, vbox(std::move(rows)), searching_ || contents_keys(), ui_);
}

ftxui::Element TheoryScreen::text_panel(std::size_t current, int width, int height) {
    using namespace ftxui;
    const auto& s = theory::sections()[current];
    auto title = (s.number.empty() ? std::string{} : s.number + " ") + s.title + "  {#" + s.id + "}";
    if (!s.tier.empty()) title += "  [" + s.tier + "]";
    const int inner = std::max(10, width - 4);
    const auto* card = mode_ == Mode::tutorial ? card_for_section(s.id) : nullptr;
    visible_rows_ = std::max(1, height - 2 - (card ? 1 : 0));
    clamp_scroll();
    DocumentSpec spec{blocks_for(current), inner, scroll_, hscroll_, glyphs_, focused_link_, nullptr, {}, visible_rows_};
    auto body = document(spec, ui_, &doc_) | reflect(doc_box_);
    clamp_scroll();
    if (card) {
        const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
        std::size_t more = 0;
        for (const auto& b : theory::block_catalog())
            if (b.section == s.id && b.id != card->id && theory::find_card(b.id)) ++more;
        const auto label = fmt::format(fmt::runtime(tr(more ? "tui.theory.card_link_more" : "tui.theory.card_link")), fmt::arg("mark", ascii ? "[i]" : "◈"),
                                       fmt::arg("title", card->title), fmt::arg("more", more));
        body = vbox({text(clip(label, inner, ascii)) | fg(ui_.theme.accent, ui_) | reflect(card_link_box_), body});
    }
    return panel(title, {{tr("tui.theory.chip.links"), false}}, body, !searching_ && !contents_keys(), ui_);
}

ftxui::Element TheoryScreen::OnRender() {
    using namespace ftxui;
    sync_version();
    doc_box_ = references_box_ = kNoBox;
    blocks_box_ = blocks_up_box_ = blocks_down_box_ = card_link_box_ = kNoBox;
    toc_box_ = toc_up_box_ = toc_down_box_ = kNoBox;
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    int height = std::max(3, rows - 3);
    const auto current = current_section();
    const bool single = breakpoint_ <= Breakpoint::small || maximized_;
    const bool show_contents = searching_ || small_contents_ || (maximized_ && toc_focus_);
    if (mode_ == Mode::under_the_hood) {
        auto* p = card_page();
        if (p) refresh_values(*p);
        auto top = origin_line(p, cols);
        height = std::max(3, height - 1);
        if (single) {
            toc_boxes_.clear();
            block_boxes_.clear();
            if (show_contents || !p) return vbox({top, contents_panel(current, cols, height) | flex});
            return vbox({top, card_panel(p, cols, height) | flex});
        }
        return vbox({top, hbox({contents_panel(current, kContentsWidth, height) | size(WIDTH, EQUAL, kContentsWidth), text(" "),
                                card_panel(p, cols - kContentsWidth - 1, height) | flex}) |
                              flex});
    }
    if (single) {
        toc_boxes_.clear();
        if (show_contents) return contents_panel(current, cols, height);
        return text_panel(current, cols, height);
    }
    return hbox({contents_panel(current, kContentsWidth, height) | size(WIDTH, EQUAL, kContentsWidth), text(" "),
                 text_panel(current, cols - kContentsWidth - 1, height) | flex});
}

bool TheoryScreen::on_mouse(ftxui::Mouse m) {
    using ftxui::Mouse;
    if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
        if (blocks_box_.Contain(m.x, m.y)) return wheel_window(blocks_first_, m.button == Mouse::WheelDown), true;
        if (toc_box_.Contain(m.x, m.y)) return wheel_window(toc_first_, m.button == Mouse::WheelDown), true;
        if (!doc_box_.Contain(m.x, m.y)) return false;
        scroll_ += m.button == Mouse::WheelDown ? 3 : -3;
        clamp_scroll();
        return true;
    }
    if (m.button != Mouse::Left || m.motion != Mouse::Pressed) return false;
    for (const auto& [box, section] : toc_boxes_)
        if (box.Contain(m.x, m.y)) return activate_section(section);
    if (references_box_.Contain(m.x, m.y)) return open_section("references"), true;
    if (card_link_box_.Contain(m.x, m.y)) return open_section_card();
    if (toc_up_box_.Contain(m.x, m.y)) return toc_first_ -= std::min(toc_first_, toc_page_), true;
    if (toc_down_box_.Contain(m.x, m.y)) return toc_first_ += toc_page_, true;
    if (blocks_up_box_.Contain(m.x, m.y)) return blocks_first_ -= std::min(blocks_first_, blocks_page_), true;
    if (blocks_down_box_.Contain(m.x, m.y)) return blocks_first_ += blocks_page_, true;
    for (const auto& [box, id] : block_boxes_)
        if (box.Contain(m.x, m.y)) return activate_block(id);
    for (std::size_t i = 0; i < doc_.links.size(); ++i)
        if (doc_.links[i].box.Contain(m.x, m.y)) {
            focused_link_ = i;
            follow(doc_.links[i].target);
            return true;
        }
    return false;
}

bool TheoryScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    if (e.is_mouse()) return on_mouse(e.mouse());
    if (searching_) {
        if (e == Event::Escape) {
            searching_ = false;
            query_.clear();
            hits_.clear();
            return true;
        }
        if (e == Event::Return) {
            if (!hits_.empty()) {
                const auto hit = hits_[std::min(selected_hit_, hits_.size() - 1)];
                searching_ = false;
                query_.clear();
                hits_.clear();
                activate_section(hit);
            }
            return true;
        }
        const auto page = static_cast<std::size_t>(std::max(1, visible_rows_ - 2));
        const auto last_hit = hits_.empty() ? 0 : hits_.size() - 1;
        if (e == Event::ArrowDown) return selected_hit_ = std::min(selected_hit_ + 1, last_hit), true;
        if (e == Event::ArrowUp) return selected_hit_ -= selected_hit_ > 0 ? 1 : 0, true;
        if (e == Event::PageDown) return selected_hit_ = std::min(selected_hit_ + page, last_hit), true;
        if (e == Event::PageUp) return selected_hit_ -= std::min(selected_hit_, page), true;
        if (e == Event::Home) return selected_hit_ = 0, true;
        if (e == Event::End) return selected_hit_ = last_hit, true;
        if (e == Event::Backspace) {
            pop_codepoint(query_);
        } else if (e.is_character()) {
            query_ += e.character();
        } else {
            return true;
        }
        hits_ = theory::search_sections(query_);
        selected_hit_ = 0;
        return true;
    }
    if (e == Event::Escape && maximized_) return maximized_ = false, true;
    if (e == Event::Character('z')) {
        if (breakpoint_ <= Breakpoint::small) return false;
        return maximized_ = !maximized_, true;
    }
    if (e == Event::Character('t')) {
        if (breakpoint_ <= Breakpoint::small) {
            small_contents_ = !small_contents_;
            toc_focus_ = small_contents_;
        } else {
            toc_focus_ = !toc_focus_;
        }
        if (toc_focus_) start_cursor();
        return true;
    }
    if (contents_keys() && contents_key(e)) return true;
    if (mode_ == Mode::under_the_hood) {
        if (e == Event::Character('n')) return fold_numbers_ = !fold_numbers_, true;
        if (e == Event::Character('o')) return open_source(), true;
        if (e == Event::Escape) {
            const auto* p = card_page();
            if (p && !p->origin.empty() && ui_.navigate) {
                ui_.navigate(p->origin, {});
                return true;
            }
            return false;
        }
    }
    if (mode_ == Mode::tutorial && e == Event::Character('i')) return open_section_card();
    if (e == Event::Character('/')) {
        searching_ = true;
        query_.clear();
        hits_.clear();
        selected_hit_ = 0;
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse) {
        const auto n = doc_.links.size();
        if (n == 0) return true;
        if (!focused_link_) focused_link_ = e == Event::Tab ? 0 : n - 1;
        else focused_link_ = e == Event::Tab ? (*focused_link_ + 1) % n : (*focused_link_ + n - 1) % n;
        const int line = doc_.links[*focused_link_].line;
        if (line >= 0 && line < scroll_) scroll_ = line;
        if (line >= scroll_ + visible_rows_) scroll_ = line - visible_rows_ + 1;
        return true;
    }
    if (e == Event::Return) {
        if (focused_link_ && *focused_link_ < doc_.links.size()) follow(doc_.links[*focused_link_].target);
        return true;
    }
    if (e == Event::Character('b') || e == Event::Backspace) return nav_->back(), true;
    if (e == Event::Character('f')) return nav_->forward(), true;
    if (e == Event::ArrowDown) return scroll_ += 1, clamp_scroll(), true;
    if (e == Event::ArrowUp) return scroll_ -= 1, clamp_scroll(), true;
    if (e == Event::PageDown) return scroll_ += std::max(1, visible_rows_ - 2), clamp_scroll(), true;
    if (e == Event::PageUp) return scroll_ -= std::max(1, visible_rows_ - 2), clamp_scroll(), true;
    if (e == Event::Home) return scroll_ = 0, true;
    if (e == Event::End) return scroll_ = std::max(0, doc_.height - visible_rows_), true;
    if (e == Event::ArrowRight) return hscroll_ = std::min(hscroll_ + 4, doc_.max_hscroll), true;
    if (e == Event::ArrowLeft) return hscroll_ = std::max(0, hscroll_ - 4), true;
    if (e == Event::Character('g')) {
        glyphs_ = glyphs_ == theory::MathGlyphs::unicode ? theory::MathGlyphs::ascii : theory::MathGlyphs::unicode;
        return true;
    }
    return false;
}

std::vector<TheoryScreen::Entry> TheoryScreen::toc_entries() const {
    if (query_.empty()) return contents(current_section());
    std::vector<Entry> out;
    for (auto h : hits_) out.push_back({h, 0});
    return out;
}

bool TheoryScreen::contents_keys() const {
    if (toc_focus_) return true;
    if (breakpoint_ > Breakpoint::small && !maximized_) return false;
    return small_contents_ || (mode_ == Mode::under_the_hood && !nav_->last(TheoryLink::Kind::card));
}

void TheoryScreen::start_cursor() {
    const auto entries = toc_entries();
    const auto current = current_section();
    toc_cursor_ = 0;
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].section == current) toc_cursor_ = i;
    toc_follow_cursor_ = true;
}

bool TheoryScreen::activate_section(std::size_t index) {
    const auto& id = theory::sections()[index].id;
    small_contents_ = toc_focus_ = false;
    if (mode_ == Mode::under_the_hood) {
        if (const auto* card = card_for_section(id)) return activate_block(std::string{card->id});
        open_section(id);
        if (ui_.navigate) ui_.navigate("tutorial", {});
        return true;
    }
    open_section(id);
    return true;
}

bool TheoryScreen::activate_block(const std::string& id) {
    nav_->open({{TheoryLink::Kind::card, id}, std::make_shared<theory::LiveValues>(), {}, {}, 0});
    small_contents_ = toc_focus_ = false;
    return true;
}

bool TheoryScreen::contents_key(ftxui::Event e) {
    using ftxui::Event;
    const auto entries = toc_entries();
    const auto cards = mode_ == Mode::under_the_hood ? catalog_cards() : std::vector<const theory::Card*>{};
    const auto n = entries.size() + cards.size();
    if (e == Event::Escape) {
        if (!toc_focus_ && !small_contents_) return false;
        toc_focus_ = small_contents_ = false;
        return true;
    }
    if (n == 0) return false;
    const auto page = static_cast<std::size_t>(std::max(1, visible_rows_ - 2));
    auto& c = toc_cursor_;
    c = std::min(c, n - 1);
    const auto moved = [&] { return toc_follow_cursor_ = true; };
    if (e == Event::ArrowDown) return c = std::min(c + 1, n - 1), moved();
    if (e == Event::ArrowUp) return c -= c > 0 ? 1 : 0, moved();
    if (e == Event::PageDown) return c = std::min(c + page, n - 1), moved();
    if (e == Event::PageUp) return c -= std::min(c, page), moved();
    if (e == Event::Home) return c = 0, moved();
    if (e == Event::End) return c = n - 1, moved();
    if (e == Event::Return) return c < entries.size() ? activate_section(entries[c].section) : activate_block(std::string{cards[c - entries.size()]->id});
    return false;
}

std::vector<JumpTarget> TheoryScreen::jump_targets() const {
    std::vector<JumpTarget> out;
    for (const auto& l : doc_.links)
        if (l.box.x_max >= l.box.x_min && l.box.y_max >= l.box.y_min) out.push_back({l.box, l.label});
    return out;
}

std::vector<MenuItem> TheoryScreen::context_menu(int x, int y) {
    for (const auto& f : doc_.formulas)
        if (f.box.Contain(x, y)) {
            const auto latex = f.latex;
            return {{tr("tui.theory.copy_latex"), "", [this, latex] {
                         if (ui_.copy_text) ui_.copy_text(latex);
                     }}};
        }
    return {};
}

ftxui::Box TheoryScreen::area(std::string_view id) const {
    if (id == "document") return doc_box_;
    if (id.starts_with("view:"))
        for (const auto& l : doc_.links)
            if (l.target == id) return l.box;
    if (id.starts_with("formula:")) {
        const auto n = static_cast<std::size_t>(std::atoi(std::string{id.substr(8)}.c_str()));
        return n < doc_.formulas.size() ? doc_.formulas[n].box : kNoBox;
    }
    if (id.starts_with("block:"))
        for (const auto& [box, block] : block_boxes_)
            if (block == id.substr(6)) return box;
    if (id == "references") return references_box_;
    if (id == "blocks") return blocks_box_;
    if (id == "card_link") return card_link_box_;
    if (id == "doc") return doc_box_;
    if (id == "toc") return toc_box_;
    if (id == "toc:up") return toc_up_box_;
    if (id == "toc:down") return toc_down_box_;
    if (id == "blocks:up") return blocks_up_box_;
    if (id == "blocks:down") return blocks_down_box_;
    if (id.starts_with("toc:"))
        for (const auto& [box, section] : toc_boxes_)
            if (theory::sections()[section].id == id.substr(4)) return box;
    return kNoBox;
}

std::shared_ptr<TheoryScreen> make_theory_screen(UiContext& ui, std::shared_ptr<TheoryNavigator> nav, TheoryScreen::Mode mode) {
    return std::make_shared<TheoryScreen>(ui, std::move(nav), mode);
}

}  // namespace signals::tui
