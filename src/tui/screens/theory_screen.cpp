#include "tui/screens/theory_screen.hpp"

#include <algorithm>
#include <filesystem>
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

}  // namespace

TheoryScreen::TheoryScreen(UiContext& ui, std::shared_ptr<TheoryNavigator> nav, Mode mode)
    : ui_{ui}, nav_{std::move(nav)}, mode_{mode},
      glyphs_{ui.caps.glyphs == GlyphMode::ascii ? theory::MathGlyphs::ascii : theory::MathGlyphs::unicode} {}

std::string TheoryScreen::tab_label() const { return tr(mode_ == Mode::tutorial ? "tui.sub.tutorial" : "tui.sub.under_the_hood"); }

std::vector<Hint> TheoryScreen::hints() const {
    std::vector<Hint> out{{"/", tr("tui.theory.hint.search")},  {"Tab", tr("tui.theory.hint.links")},  {"Enter", tr("tui.theory.hint.follow")},
                          {"b/f", tr("tui.theory.hint.history")}, {"↑↓", tr("tui.theory.hint.scroll")}, {"←→", tr("tui.theory.hint.formulas")},
                          {"g", tr("tui.theory.hint.glyphs")}};
    if (mode_ == Mode::under_the_hood) {
        out.push_back({"n", tr("tui.theory.hint.numbers")});
        out.push_back({"o", tr("tui.theory.hint.source")});
        out.push_back({"Esc", tr("tui.theory.hint.back")});
    }
    if (breakpoint_ <= Breakpoint::small) out.push_back({"t", tr("tui.theory.hint.contents")});
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

std::string TheoryScreen::card_markdown(const theory::Card& card) const {
    std::string md;
    const auto numbers = tr("theory.card.numbers");
    for (const auto& s : card.sections) {
        md += "## " + s.heading + "\n\n";
        if (fold_numbers_ && s.heading == numbers) {
            md += "*" + tr("tui.theory.folded") + "*\n\n";
            continue;
        }
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

ftxui::Element TheoryScreen::blocks_list(const theory::Card* current, int width, int rows) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    Elements out{text(tr("tui.theory.blocks")) | fg(th.dim, ui_)};
    std::vector<const theory::Card*> all;
    for (const auto& b : theory::block_catalog())
        if (const auto* c = theory::find_card(b.id)) all.push_back(c);
    std::size_t at = 0;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i] == current) at = i;
    const auto room = static_cast<std::size_t>(std::max(1, rows - 1));
    const std::size_t first = at >= room ? at - room + 1 : 0;
    block_boxes_.clear();
    block_boxes_.reserve(all.size());
    for (std::size_t i = first; i < all.size() && i - first < room; ++i) {
        const bool on = all[i] == current;
        auto label = clip(all[i]->title, width - 2, ascii);
        auto row = text(label);
        row = on ? row | bold | fg(th.bg, ui_) | bg(th.accent, ui_) : row | fg(th.accent, ui_);
        block_boxes_.emplace_back(ftxui::Box{}, all[i]->id);
        out.push_back(row | reflect(block_boxes_.back().first));
    }
    return vbox(std::move(out));
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
    card_blocks_ = theory::parse_markdown(card_markdown(*card));
    clamp_scroll();
    DocumentSpec spec{card_blocks_, inner, scroll_, hscroll_, glyphs_, focused_link_, p->values.get(), "─", visible_rows_};
    Elements body{document(spec, ui_, &doc_) | reflect(doc_box_) | flex};
    if (!notice_.empty()) body.push_back(text(notice_) | fg(th.warn, ui_));
    clamp_scroll();
    return panel(title, {{tr("tui.theory.chip.links"), false}}, vbox(std::move(body)), !searching_, ui_);
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
    std::size_t first = 0;
    if (static_cast<int>(selected) >= room) first = selected - static_cast<std::size_t>(room) + 1;
    toc_boxes_.clear();
    toc_boxes_.reserve(entries.size() + 1);
    for (std::size_t i = first; i < entries.size() && static_cast<int>(i - first) < room; ++i) {
        const auto& s = all[entries[i].section];
        const bool on = query_.empty() ? entries[i].section == current : i == selected;
        std::string mark = on && query_.empty() ? (ascii ? " <" : " ◀") : "";
        if (mode_ == Mode::under_the_hood && card_for_section(s.id)) mark = (ascii ? " [i]" : " ⓘ") + mark;
        auto label = std::string(static_cast<std::size_t>(2 * entries[i].depth), ' ') + entry_label(s);
        label = clip(label, inner - static_cast<int>(theory::display_width(mark)), ascii) + mark;
        auto row = text(label);
        row = on ? row | bold | fg(th.bg, ui_) | bg(th.accent, ui_) : row | fg(entries[i].depth ? th.fg : th.fg, ui_);
        toc_boxes_.emplace_back(ftxui::Box{}, entries[i].section);
        rows.push_back(row | reflect(toc_boxes_.back().first));
    }
    if (entries.empty()) rows.push_back(text(tr("tui.help.nothing")) | fg(th.dim, ui_));
    rows.push_back(filler());
    std::string rule;
    for (int i = 0; i < inner; ++i) rule += glyph(Glyph::h, ui_.caps.glyphs);
    rows.push_back(text(rule) | fg(th.dim, ui_));
    if (mode_ == Mode::under_the_hood) {
        const auto* p = nav_->last(TheoryLink::Kind::card);
        rows.push_back(blocks_list(p ? theory::find_card(p->link.id) : nullptr, inner, block_rows + 1));
    } else {
        rows.push_back(text(tr("tui.theory.references")) | fg(th.accent, ui_) | reflect(references_box_));
    }
    const auto title = mode_ == Mode::tutorial ? fmt::format(fmt::runtime(tr("tui.theory.contents")), fmt::arg("count", total))
                                               : tr("tui.theory.contents_short");
    return panel(title, {}, vbox(std::move(rows)), searching_, ui_);
}

ftxui::Element TheoryScreen::text_panel(std::size_t current, int width, int height) {
    using namespace ftxui;
    const auto& s = theory::sections()[current];
    auto title = (s.number.empty() ? std::string{} : s.number + " ") + s.title + "  {#" + s.id + "}";
    if (!s.tier.empty()) title += "  [" + s.tier + "]";
    const int inner = std::max(10, width - 4);
    visible_rows_ = std::max(1, height - 2);
    clamp_scroll();
    DocumentSpec spec{blocks_for(current), inner, scroll_, hscroll_, glyphs_, focused_link_, nullptr, {}, visible_rows_};
    auto body = document(spec, ui_, &doc_) | reflect(doc_box_);
    clamp_scroll();
    return panel(title, {{tr("tui.theory.chip.links"), false}}, body, !searching_, ui_);
}

ftxui::Element TheoryScreen::OnRender() {
    using namespace ftxui;
    sync_version();
    doc_box_ = references_box_ = kNoBox;
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    int height = std::max(3, rows - 3);
    const auto current = current_section();
    if (mode_ == Mode::under_the_hood) {
        auto* p = card_page();
        if (p) refresh_values(*p);
        auto top = origin_line(p, cols);
        height = std::max(3, height - 1);
        if (breakpoint_ <= Breakpoint::small) {
            toc_boxes_.clear();
            block_boxes_.clear();
            if (small_contents_ || searching_) return vbox({top, contents_panel(current, cols, height)});
            return vbox({top, card_panel(p, cols, height)});
        }
        return vbox({top, hbox({contents_panel(current, kContentsWidth, height) | size(WIDTH, EQUAL, kContentsWidth), text(" "),
                                card_panel(p, cols - kContentsWidth - 1, height) | flex})});
    }
    if (breakpoint_ <= Breakpoint::small) {
        toc_boxes_.clear();
        if (small_contents_ || searching_) return contents_panel(current, cols, height);
        return text_panel(current, cols, height);
    }
    return hbox({contents_panel(current, kContentsWidth, height) | size(WIDTH, EQUAL, kContentsWidth), text(" "),
                 text_panel(current, cols - kContentsWidth - 1, height) | flex});
}

bool TheoryScreen::on_mouse(ftxui::Mouse m) {
    using ftxui::Mouse;
    if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
        if (!doc_box_.Contain(m.x, m.y)) return false;
        scroll_ += m.button == Mouse::WheelDown ? 3 : -3;
        clamp_scroll();
        return true;
    }
    if (m.button != Mouse::Left || m.motion != Mouse::Pressed) return false;
    for (const auto& [box, section] : toc_boxes_)
        if (box.Contain(m.x, m.y)) {
            const auto& id = theory::sections()[section].id;
            small_contents_ = false;
            if (mode_ == Mode::under_the_hood) {
                if (const auto* card = card_for_section(id)) {
                    nav_->open({{TheoryLink::Kind::card, card->id}, std::make_shared<theory::LiveValues>(), {}, {}, 0});
                    return true;
                }
                open_section(id);
                if (ui_.navigate) ui_.navigate("tutorial", {});
                return true;
            }
            open_section(id);
            return true;
        }
    if (references_box_.Contain(m.x, m.y)) return open_section("references"), true;
    for (const auto& [box, id] : block_boxes_)
        if (box.Contain(m.x, m.y)) {
            nav_->open({{TheoryLink::Kind::card, id}, std::make_shared<theory::LiveValues>(), {}, {}, 0});
            small_contents_ = false;
            return true;
        }
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
                open_section(theory::sections()[hits_[std::min(selected_hit_, hits_.size() - 1)]].id);
                if (mode_ == Mode::under_the_hood && ui_.navigate) ui_.navigate("tutorial", {});
                searching_ = false;
                query_.clear();
                hits_.clear();
                small_contents_ = false;
            }
            return true;
        }
        if (e == Event::ArrowDown) {
            if (!hits_.empty()) selected_hit_ = std::min(selected_hit_ + 1, hits_.size() - 1);
            return true;
        }
        if (e == Event::ArrowUp) {
            if (selected_hit_ > 0) --selected_hit_;
            return true;
        }
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
    if (e == Event::Character('t') && breakpoint_ <= Breakpoint::small) return small_contents_ = !small_contents_, true;
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
    if (id.starts_with("toc:"))
        for (const auto& [box, section] : toc_boxes_)
            if (theory::sections()[section].id == id.substr(4)) return box;
    return kNoBox;
}

std::shared_ptr<TheoryScreen> make_theory_screen(UiContext& ui, std::shared_ptr<TheoryNavigator> nav, TheoryScreen::Mode mode) {
    return std::make_shared<TheoryScreen>(ui, std::move(nav), mode);
}

}  // namespace signals::tui
