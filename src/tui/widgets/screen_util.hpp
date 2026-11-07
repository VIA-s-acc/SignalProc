#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "tui/shell/screen.hpp"
#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

inline const ftxui::Box kNoBox{1, 0, 1, 0};
inline bool valid(const ftxui::Box& b) { return b.x_max >= b.x_min && b.y_max >= b.y_min; }

std::string group(std::uint64_t n);
std::string hz_text(double f);
std::string time_text(double t, double span);
std::string band_name(std::size_t k, std::size_t levels, GlyphMode g);
std::pair<double, double> band_range(std::size_t k, std::size_t levels, double fs);
std::string bar(double f, int width, GlyphMode g);
Rgb tint(Rgb c, Rgb toward, double t);

struct Span {
    std::string text;
    Rgb color;
    bool bold = false;
};
ftxui::Element line(const std::vector<Span>& spans, const UiContext& ui);
ftxui::Element centered(std::vector<ftxui::Element> rows);

std::optional<std::string> explain_note(std::string_view screen, std::string_view id);

class Notes {
public:
    explicit Notes(std::string screen) : screen_{std::move(screen)} {}
    void clear() { items_.clear(); }
    ftxui::Box* box(std::string id);
    ftxui::Element mark(std::string id, ftxui::Element e);
    std::optional<std::string> at(int x, int y) const;
    std::optional<std::string> explain(std::string_view id) const { return explain_note(screen_, id); }

private:
    std::string screen_;
    std::deque<std::pair<std::string, ftxui::Box>> items_;
};

}  // namespace signals::tui
