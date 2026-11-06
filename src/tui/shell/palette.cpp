#include "tui/widgets/text_util.hpp"
#include "tui/shell/palette.hpp"

#include <algorithm>
#include <utility>

#include "i18n/i18n.hpp"
#include "tui/shell/panel.hpp"

namespace signals::tui {
namespace {

constexpr std::size_t kVisible = 12;

std::u32string lower_codepoints(std::string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
        for (std::size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        if (cp >= U'A' && cp <= U'Z') cp += 32;
        else if (cp >= 0x0410 && cp <= 0x042F) cp += 0x20;
        else if (cp == 0x0401) cp = 0x0451;
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::optional<std::size_t> first_match(const std::u32string& text, const std::u32string& query) {
    if (query.empty()) return 0;
    std::size_t at = 0, first = 0;
    for (std::size_t q = 0; q < query.size(); ++q) {
        const auto pos = text.find(query[q], at);
        if (pos == std::u32string::npos) return std::nullopt;
        if (q == 0) first = pos;
        at = pos + 1;
    }
    return first;
}

}  // namespace

void pop_codepoint(std::string& s) {
    while (!s.empty()) {
        const auto c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

std::vector<const Command*> filter_commands(const std::vector<Command>& all, std::string_view query) {
    const auto q = lower_codepoints(query);
    std::vector<std::pair<std::pair<bool, std::size_t>, const Command*>> ranked;
    for (const auto& c : all) {
        const auto title = lower_codepoints(c.title), id = lower_codepoints(c.id);
        auto pos = first_match(title, q);
        if (!pos) pos = first_match(id, q);
        if (!pos) continue;
        const bool scattered = !q.empty() && title.find(q) == std::u32string::npos && id.find(q) == std::u32string::npos;
        ranked.emplace_back(std::pair{scattered, *pos}, &c);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<const Command*> out;
    for (const auto& [pos, c] : ranked) out.push_back(c);
    return out;
}

void Palette::open(std::vector<Command> commands, std::string filter) {
    commands_ = std::move(commands);
    query_ = std::move(filter);
    selected_ = 0;
    first_ = 0;
    prompt_.clear();
    path_done_ = nullptr;
    open_ = true;
}

void Palette::ask_path(std::string prompt, std::function<void(std::string)> done) { ask_text(std::move(prompt), {}, std::move(done)); }

void Palette::ask_text(std::string prompt, std::string initial, std::function<void(std::string)> done) {
    query_ = std::move(initial);
    prompt_ = std::move(prompt);
    path_done_ = std::move(done);
    open_ = true;
}

void Palette::close() {
    open_ = false;
    path_done_ = nullptr;
}

bool Palette::on_event(const ftxui::Event& e) {
    if (e == ftxui::Event::Escape) {
        close();
        return true;
    }
    if (e == ftxui::Event::Backspace) {
        pop_codepoint(query_);
        selected_ = first_ = 0;
        return true;
    }
    if (path_done_) {
        if (e == ftxui::Event::Return) {
            auto done = std::move(path_done_);
            const auto path = query_;
            close();
            if (!path.empty()) done(path);
            return true;
        }
        if (e.is_character()) query_ += e.character();
        return true;
    }
    const auto matches = filter_commands(commands_, query_);
    const auto n = matches.size();
    auto wheel = [&](ftxui::Mouse::Button b) {
        auto copy = e;
        return copy.is_mouse() && copy.mouse().button == b;
    };
    if (e == ftxui::Event::ArrowDown || wheel(ftxui::Mouse::WheelDown)) return select(selected_ + 1, n), true;
    if (e == ftxui::Event::ArrowUp || wheel(ftxui::Mouse::WheelUp)) return select(selected_ == 0 ? 0 : selected_ - 1, n), true;
    if (e == ftxui::Event::PageDown) return select(selected_ + kVisible, n), true;
    if (e == ftxui::Event::PageUp) return select(selected_ > kVisible ? selected_ - kVisible : 0, n), true;
    if (e == ftxui::Event::Home) return select(0, n), true;
    if (e == ftxui::Event::End) return select(n == 0 ? 0 : n - 1, n), true;
    if (e.is_mouse()) return true;
    if (e == ftxui::Event::Return) {
        if (selected_ < matches.size()) {
            auto run = matches[selected_]->run;
            close();
            if (run) run();
        }
        return true;
    }
    if (e.is_character()) {
        query_ += e.character();
        selected_ = first_ = 0;
    }
    return true;
}

void Palette::select(std::size_t index, std::size_t count) {
    if (count == 0) {
        selected_ = first_ = 0;
        return;
    }
    selected_ = std::min(index, count - 1);
    if (selected_ < first_) first_ = selected_;
    if (selected_ >= first_ + kVisible) first_ = selected_ - kVisible + 1;
}

ftxui::Element Palette::render(const UiContext& ui) const {
    using namespace ftxui;
    Elements rows;
    if (path_done_) {
        const auto cps = split_codepoints(query_);
        const std::size_t room = 56 - std::min<std::size_t>(40, split_codepoints(prompt_).size());
        std::string tail;
        for (std::size_t i = cps.size() > room ? cps.size() - room : 0; i < cps.size(); ++i) tail += cps[i];
        rows.push_back(text(prompt_ + (cps.size() > room ? "…" : "") + tail + "▏"));
        rows.push_back(text(tr("tui.palette.path_hint")) | fg(ui.theme.dim, ui));
        return panel(tr("tui.palette.title"), {}, vbox(std::move(rows)) | size(WIDTH, GREATER_THAN, 60), true, ui);
    }
    rows.push_back(text(": " + query_ + "▏") | bold);
    const auto matches = filter_commands(commands_, query_);
    for (std::size_t i = first_; i < matches.size() && i < first_ + kVisible; ++i) {
        auto row = text(" " + matches[i]->title + " ");
        rows.push_back(i == selected_ ? row | bold | fg(ui.theme.bg, ui) | bg(ui.theme.accent, ui) : row);
    }
    if (matches.empty()) rows.push_back(text(tr("tui.help.nothing")) | fg(ui.theme.dim, ui));
    auto keys = tr("tui.palette.keys");
    if (matches.size() > kVisible) keys += " · " + std::to_string(selected_ + 1) + " / " + std::to_string(matches.size());
    rows.push_back(text(keys) | fg(ui.theme.dim, ui));
    return panel(tr("tui.palette.title"), {}, vbox(std::move(rows)) | size(WIDTH, GREATER_THAN, 60), true, ui);
}

std::filesystem::path expand_path(std::string text, const EnvLookup& env) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (!text.empty() && blank(text.back())) text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && blank(text[start])) ++start;
    text.erase(0, start);
    if (text.size() >= 2 && (text.front() == '\'' || text.front() == '"') && text.back() == text.front()) {
        text = text.substr(1, text.size() - 2);
    } else {
        std::string plain;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;
            plain += text[i];
        }
        text = std::move(plain);
    }
    if (!text.empty() && text.front() == '~' && (text.size() == 1 || text[1] == '/'))
        if (auto home = env("HOME")) text = *home + text.substr(1);
    return text;
}

}  // namespace signals::tui
