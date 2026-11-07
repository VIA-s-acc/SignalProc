#include "tui/screens/diagram_screen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <numbers>

#include <fmt/format.h>

#include "core/design/families.hpp"
#include "i18n/i18n.hpp"
#include "tui/shell/errors.hpp"
#include "tui/shell/panel.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/plot.hpp"
#include "tui/widgets/screen_util.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr std::array<const char*, 7> kBanks{"designer", "haar", "db2", "db4", "sym4", "bior2.2", "bior4.4"};
constexpr std::array<std::size_t, 7> kLengths{8, 16, 32, 64, 256, 1024, 4096};
constexpr std::size_t kNumbersOverWires = 32;
constexpr double kPrTol = 1e-9;

std::string wire_label(std::string_view id) {
    static const std::map<std::string, std::string, std::less<>> labels{
        {"x", "x"},   {"y0", "y₀"}, {"y1", "y₁"}, {"a", "a"},   {"d", "d"},   {"u0", "u₀"}, {"u1", "u₁"},
        {"v0", "v₀"}, {"v1", "v₁"}, {"xh", "x̂"},  {"xe", "xₑ"}, {"xo", "xₒ"}, {"we", "wₑ"}, {"wo", "wₒ"}};
    const auto it = labels.find(id);
    return it == labels.end() ? std::string{id} : it->second;
}

double energy(const std::vector<double>& v) {
    double e = 0;
    for (double x : v) e += x * x;
    return e;
}

}  // namespace

DiagramScreen::DiagramScreen(UiContext& ui, std::shared_ptr<DesignerModel> designer, std::shared_ptr<SignalModel> signal)
    : ui_{ui}, designer_{std::move(designer)}, signal_{std::move(signal)} {}

std::string DiagramScreen::tab_label() const { return tr("tui.sub.diagram"); }

std::vector<Hint> DiagramScreen::hints() const {
    return {{"P", tr("tui.diagram.hint.polyphase")}, {"w", tr("tui.diagram.hint.bank")},   {"i", tr("tui.diagram.hint.input")},
            {"+ −", tr("tui.diagram.hint.length")}, {"v", tr("tui.diagram.hint.view")},     {"←→", tr("tui.diagram.hint.wire")},
            {tr("tui.signal.key.click"), tr("tui.diagram.hint.click")}};
}

std::optional<TheoryLink> DiagramScreen::theory_link() const { return TheoryLink{TheoryLink::Kind::card, "filter_bank"}; }

void DiagramScreen::publish(theory::LiveValues& v) const {
    v.source = tab_label() + " ▸ " + bank_name_;
    v.set("block.bank", bank_name_);
    if (!result_) return;
    v.set("block.length", std::to_string(bank_length_));
    v.set("block.delay", std::to_string(result_->delay));
}

std::size_t DiagramScreen::length() const { return kLengths[length_]; }

ftxui::Box DiagramScreen::area(std::string_view id) const {
    if (id == "diagram_panel") return panel_boxes_[diagram_panel_id];
    if (id == "probe_panel") return panel_boxes_[probe_panel_id];
    if (id == "probe_time_plot") return zoom_.box("probe_time");
    if (id == "probe_spectrum_plot") return zoom_.box("probe_spectrum");
    ftxui::Box out{0, -1, 0, -1};
    bool any = false;
    for (const auto& [name, b] : hits_) {
        if (name != id || b.x_max < b.x_min) continue;
        out = any ? ftxui::Box::Union(out, b) : b;
        any = true;
    }
    return out;
}

std::vector<std::string> DiagramScreen::wires() const {
    if (polyphase_) return {"x", "xe", "xo", "a", "d", "we", "wo", "xh"};
    return {"x", "y0", "y1", "a", "d", "u0", "u1", "v0", "v1", "xh"};
}

void DiagramScreen::select(const std::string& id) {
    static const std::map<std::string, std::string, std::less<>> outputs{
        {"H0", "y0"}, {"H1", "y1"}, {"D0", "a"}, {"D1", "d"}, {"U0", "u0"}, {"U1", "u1"}, {"F0", "v0"},
        {"F1", "v1"}, {"S", "xh"},  {"SPLIT", "xe"}, {"E", "a"}, {"R", "we"}, {"MERGE", "xh"}};
    std::string wire = id;
    if (const auto it = outputs.find(id); it != outputs.end()) wire = it->second;
    const auto all = wires();
    probe_ = std::find(all.begin(), all.end(), wire) != all.end() ? wire : std::string{"a"};
}

void DiagramScreen::refresh() {
    notice_.clear();
    std::optional<core::FilterBank> bank;
    std::string key;
    std::shared_ptr<const DesignerResult> source;
    if (bank_ == 0) {
        if (auto r = designer_ ? designer_->result() : nullptr) {
            bank = r->bank;
            bank_name_ = tr("tui.diagram.designer");
            key = "designer";
            source = std::move(r);
        } else {
            notice_ = tr("tui.diagram.no_designer");
        }
    }
    if (!bank) {
        const std::string name = bank_ == 0 ? "db2" : kBanks[bank_];
        auto b = core::wavelet(name);
        if (!b) {
            notice_ = error_text(b.error());
            result_.reset();
            return;
        }
        bank = std::move(*b);
        bank_name_ = name;
        key = name;
    }
    bank_length_ = bank->length();
    const auto n = length();
    std::vector<double> x;
    const auto data = signal_ ? signal_->data() : nullptr;
    if (file_ && data && !data->samples.empty() && !data->samples.front().empty()) {
        const auto& s = data->samples.front();
        std::size_t start = 0;
        if (const auto sel = signal_->selection()) {
            start = std::min(s.size() - 1, static_cast<std::size_t>(std::max(0.0, sel->first) * data->info.sample_rate));
        } else {
            if (data != peak_data_ || n != peak_length_) {
                const auto peak = static_cast<std::size_t>(std::max_element(s.begin(), s.end(), [](float l, float r) { return std::abs(l) < std::abs(r); }) - s.begin());
                peak_start_ = peak > n / 2 ? std::min(peak - n / 2, s.size() > n ? s.size() - n : std::size_t{0}) : 0;
                peak_data_ = data;
                peak_length_ = n;
            }
            start = peak_start_;
        }
        x.assign(s.begin() + static_cast<std::ptrdiff_t>(start), s.begin() + static_cast<std::ptrdiff_t>(std::min(s.size(), start + n)));
        key += ":file:" + std::to_string(start);
    } else {
        if (file_) notice_ = tr("tui.diagram.no_file");
        x.assign(n, 0.0);
        x.front() = 1;
        key += ":impulse";
    }
    key += ":" + std::to_string(n);
    const auto shown = file_ ? data : nullptr;
    if (key == key_ && result_ && source == source_bank_ && shown == source_data_) return;
    key_ = key;
    source_bank_ = std::move(source);
    source_data_ = shown;
    auto r = run_diagram(*bank, x);
    if (!r) {
        notice_ = error_text(r.error());
        result_.reset();
        return;
    }
    result_ = std::make_shared<const DiagramResult>(std::move(*r));
}

ftxui::Element DiagramScreen::clickable(ftxui::Element e, std::function<void()> action) {
    clickables_.push_back({ftxui::Box{0, -1, 0, -1}, std::move(action)});
    return std::move(e) | ftxui::reflect(clickables_.back().box);
}

ftxui::Element DiagramScreen::chips(std::vector<Item> items) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    Elements row;
    for (auto& item : items) {
        auto t = text(item.label);
        t = item.on ? t | bold | fg(th.bg, ui_) | bg(th.trace[0], ui_) : t | fg(th.fg, ui_);
        row.push_back(clickable(t, std::move(item.action)));
        row.push_back(text(" "));
    }
    return hbox(std::move(row));
}

ftxui::Element DiagramScreen::diagram_panel(int width) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    DiagramSpec spec;
    spec.polyphase = polyphase_;
    spec.compact = width < (polyphase_ ? 104 : 90);
    spec.selected = probe_;
    if (!hover_.empty()) spec.highlight.insert(hover_);
    Elements body{bank_diagram(spec, ui_, nullptr, &hits_)};
    const int inner = std::max(10, width - 4);
    const bool ascii = ui_.caps.glyphs == GlyphMode::ascii;
    if (result_) {
        if (length() <= kNumbersOverWires && !spec.compact)
            for (const char* id : {"a", "d"}) {
                std::string s = std::string{id} + " = [";
                for (double v : result_->wire(id, polyphase_)->values) s += (s.back() == '[' ? "" : " ") + format_fixed(v, 2);
                body.push_back(text(ellipsize(s + "]", inner, ascii)) | fg(th.fg, ui_));
            }
        const bool ok = result_->error < kPrTol;
        auto status = hbox({text(fmt::format(fmt::runtime(tr("tui.diagram.status")), fmt::arg("delay", result_->delay),
                                             fmt::arg("error", result_->error < 1e-15 ? std::string{"< 10⁻¹⁵"} : format_number(result_->error, 2)))) |
                                fg(th.fg, ui_),
                            text(" " + std::string{glyph(ok ? Glyph::check : Glyph::cross, ui_.caps.glyphs)}) | fg(ok ? th.ok : th.err, ui_)});
        auto mults = text(fmt::format(fmt::runtime(tr("tui.diagram.mults")), fmt::arg("direct", format_number(result_->direct_mults, 3)),
                                      fmt::arg("poly", format_number(result_->polyphase_mults, 3)))) |
                     fg(th.dim, ui_);
        if (spec.compact) {
            body.push_back(std::move(status));
            body.push_back(std::move(mults));
        } else {
            body.push_back(hbox({std::move(status), text(" · ") | fg(th.dim, ui_), std::move(mults)}));
        }
    }
    if (!notice_.empty()) body.push_back(paragraph(notice_) | fg(th.warn, ui_));
    std::vector<Item> banks;
    for (std::size_t i = 0; i < kBanks.size(); ++i)
        banks.push_back({i == 0 ? tr("tui.diagram.designer") : std::string{kBanks[i]}, [this, i] { bank_ = i; }, bank_ == i});
    body.push_back(hbox({text(tr("tui.diagram.bank") + " ") | fg(th.dim, ui_), chips(std::move(banks)), text("(w)") | fg(th.dim, ui_)}));
    std::vector<Item> inputs{{tr("tui.diagram.input.impulse"), [this] { file_ = false; }, !file_}, {tr("tui.diagram.input.file"), [this] { file_ = true; }, file_}};
    std::vector<Item> lengths;
    for (std::size_t i = 0; i < kLengths.size(); ++i) lengths.push_back({std::to_string(kLengths[i]), [this, i] { length_ = i; }, length_ == i});
    std::vector<Item> modes{{tr("tui.diagram.direct"), [this] { polyphase_ = false; select(probe_); }, !polyphase_},
                            {tr("tui.diagram.polyphase"), [this] { polyphase_ = true; select(probe_); }, polyphase_}};
    body.push_back(hbox({text(tr("tui.diagram.input") + " ") | fg(th.dim, ui_), chips(std::move(inputs)), text("N ") | fg(th.dim, ui_),
                         chips(std::move(lengths)), chips(std::move(modes)), text("(P)") | fg(th.dim, ui_)}));
    const auto title = fmt::format(fmt::runtime(tr("tui.diagram.title")), fmt::arg("bank", bank_name_),
                                   fmt::arg("view", tr(polyphase_ ? "tui.diagram.polyphase" : "tui.diagram.direct")));
    std::vector<Chip> chips;
    if (breakpoint_ <= Breakpoint::small && !maximized_) chips.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", 1), fmt::arg("count", 2)), false});
    return panel(title, std::move(chips), vbox(std::move(body)), focus_ == diagram_panel_id, ui_) | ftxui::reflect(panel_boxes_[diagram_panel_id]);
}

ftxui::Element DiagramScreen::probe_panel(int width, int height) {
    using namespace ftxui;
    const auto& th = ui_.theme;
    const auto* w = result_ ? result_->wire(probe_, polyphase_) : nullptr;
    std::vector<Chip> views{{tr("tui.diagram.view.time"), view_ == ProbeView::time},
                            {tr("tui.diagram.view.spectrum"), view_ == ProbeView::spectrum},
                            {tr("tui.diagram.view.numbers"), view_ == ProbeView::numbers}};
    if (breakpoint_ <= Breakpoint::small && !maximized_) views.push_back({fmt::format(fmt::runtime(tr("tui.signal.chip.panel")), fmt::arg("index", 2), fmt::arg("count", 2)), false});
    if (!w || w->values.empty())
        return panel(tr("tui.diagram.probe_empty"), std::move(views), text("—") | fg(th.dim, ui_), focus_ == probe_panel_id, ui_) | reflect(panel_boxes_[probe_panel_id]);
    const auto& v = w->values;
    const auto title = fmt::format(fmt::runtime(tr("tui.diagram.probe")), fmt::arg("wire", wire_label(probe_)), fmt::arg("count", v.size()),
                                   fmt::arg("energy", format_number(energy(v), 4)));
    const auto color = th.trace[0];
    Element body;
    if (view_ == ProbeView::numbers) {
        constexpr int kCell = 20;
        const int per_row = std::max(1, (width - 4) / kCell);
        const int lines = std::max(1, height - 3);
        const auto shown = std::min(v.size(), static_cast<std::size_t>(per_row * lines) - (v.size() > static_cast<std::size_t>(per_row * lines) ? 1 : 0));
        Elements rows, row;
        for (std::size_t k = 0; k < shown; ++k) {
            row.push_back(text(pad_right(wire_label(probe_) + "[" + std::to_string(k) + "] " + format_fixed(v[k], 4), kCell)) | fg(th.fg, ui_));
            if (row.size() == static_cast<std::size_t>(per_row)) rows.push_back(hbox(std::exchange(row, {})));
        }
        if (!row.empty()) rows.push_back(hbox(std::move(row)));
        if (shown < v.size())
            rows.push_back(text(fmt::format(fmt::runtime(tr("tui.diagram.more")), fmt::arg("count", v.size() - shown))) | fg(th.dim, ui_));
        body = vbox(std::move(rows));
    } else if (view_ == ProbeView::spectrum) {
        constexpr std::size_t kBins = 256;
        constexpr double pi = std::numbers::pi;
        if (spectrum_of_ != result_ || spectrum_wire_ != probe_ || spectrum_polyphase_ != polyphase_) {
            spectrum_db_.assign(kBins, 0.0);
            for (std::size_t k = 0; k < kBins; ++k) {
                const auto step = std::polar(1.0, -pi * static_cast<double>(k) / static_cast<double>(kBins - 1));
                std::complex<double> s{0, 0};
                for (std::size_t n = v.size(); n-- > 0;) s = s * step + v[n];
                spectrum_db_[k] = 20 * std::log10(std::max(std::abs(s), 1e-15));
            }
            spectrum_of_ = result_;
            spectrum_wire_ = probe_;
            spectrum_polyphase_ = polyphase_;
        }
        tui::Trace t{};
        t.color = color;
        auto& zoom = zoom_.axis("probe_spectrum", {0, pi, AxisScale::pi});
        const double top = *std::max_element(spectrum_db_.begin(), spectrum_db_.end());
        for (std::size_t k = 0; k < kBins; ++k) {
            t.x.push_back(zoom.plot_x(pi * static_cast<double>(k) / static_cast<double>(kBins - 1)));
            t.y.push_back(std::max(spectrum_db_[k], top - 100));
        }
        PlotSpec spec;
        spec.x = zoom.axis();
        spec.y = {top - 100, top + 3};
        spec.traces.push_back(std::move(t));
        auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
        body = zoom_.track("probe_spectrum", std::move(spec), std::move(drawn));
    } else {
        tui::Trace t{};
        t.color = color;
        t.stems = v.size() <= 64;
        double lo = 0, hi = 0;
        for (std::size_t k = 0; k < v.size(); ++k) {
            t.x.push_back(static_cast<double>(k));
            t.y.push_back(v[k]);
            lo = std::min(lo, v[k]);
            hi = std::max(hi, v[k]);
        }
        const double pad = std::max(1e-12, (hi - lo) * 0.1);
        PlotSpec spec;
        spec.x = zoom_.axis("probe_time", {-0.5, static_cast<double>(v.size()) - 0.5}).axis();
        spec.y = {lo - pad, hi + pad};
        spec.traces.push_back(std::move(t));
        auto drawn = plot(spec, th, ui_.caps.color, ui_.caps.glyphs) | flex;
        body = zoom_.track("probe_time", std::move(spec), std::move(drawn));
    }
    return panel(title, std::move(views), body, focus_ == probe_panel_id, ui_) | reflect(panel_boxes_[probe_panel_id]);
}

ftxui::Element DiagramScreen::OnRender() {
    using namespace ftxui;
    clickables_.clear();
    hits_.clear();
    panel_boxes_.fill(kNoBox);
    zoom_.begin_frame();
    refresh();
    const auto [cols, rows] = ui_.terminal_size ? ui_.terminal_size() : std::pair{150, 44};
    layout_.resize(cols, rows);
    if (maximized_ || breakpoint_ <= Breakpoint::small) {
        const auto p = maximized_ ? *maximized_ : static_cast<Panel>(layout_.small_index() % panel_count);
        focus_ = p;
        if (p == diagram_panel_id) return diagram_panel(cols) | flex;
        return probe_panel(cols, std::max(4, rows - 3)) | flex;
    }
    auto top = diagram_panel(cols);
    const bool compact = cols < (polyphase_ ? 104 : 90);
    const int top_rows = (polyphase_ ? 5 : 7) + (result_ && length() <= kNumbersOverWires && !compact ? 2 : 0) + (result_ ? (compact ? 2 : 1) : 0) +
                         (notice_.empty() ? 0 : static_cast<int>(wrap_words(notice_, std::max(8, cols - 4)).size())) + 4;
    auto bottom = probe_panel(cols, std::max(4, rows - 3 - top_rows));
    return vbox({std::move(top), std::move(bottom) | flex});
}

bool DiagramScreen::OnEvent(ftxui::Event e) {
    using ftxui::Event;
    using ftxui::Mouse;
    if (e.is_mouse()) {
        const auto& m = e.mouse();
        if (zoom_.on_mouse(m)) return true;
        if (m.button == Mouse::Left && m.motion == Mouse::Pressed)
            for (int p = 0; p < panel_count; ++p)
                if (panel_boxes_[static_cast<std::size_t>(p)].Contain(m.x, m.y)) {
                    focus_ = static_cast<Panel>(p);
                    if (focus_ == probe_panel_id) zoom_.press(m.x, m.y);
                }
        std::string over;
        for (const auto& [id, b] : hits_)
            if (b.Contain(m.x, m.y)) over = id;
        if (m.motion == Mouse::Moved || m.button == Mouse::None) {
            if (over == hover_) return false;
            hover_ = over;
            return true;
        }
        if (m.button != Mouse::Left || m.motion != Mouse::Pressed) return false;
        for (auto& c : clickables_)
            if (c.box.Contain(m.x, m.y)) {
                c.action();
                return true;
            }
        if (over.empty()) return m.button == Mouse::Left;
        select(over);
        return true;
    }
    if (e == Event::Escape && maximized_) return maximized_.reset(), true;
    if (e == Event::Character('z')) {
        maximized_ = maximized_ ? std::nullopt : std::optional<Panel>{focus_};
        return true;
    }
    if (e == Event::Tab || e == Event::TabReverse) {
        if (breakpoint_ <= Breakpoint::small && !maximized_) layout_.next_panel(panel_count, e == Event::TabReverse);
        else focus_ = focus_ == diagram_panel_id ? probe_panel_id : diagram_panel_id;
        if (maximized_) maximized_ = focus_;
        return true;
    }
    if (focus_ == probe_panel_id && (e == Event::Character('0') || e == Event::Character('<') || e == Event::Character('>'))) {
        const char* id = view_ == ProbeView::spectrum ? "probe_spectrum" : view_ == ProbeView::time ? "probe_time" : nullptr;
        if (id) return zoom_.on_key(e, id);
    }
    if (e == Event::Character('p') || e == Event::Character('P')) {
        polyphase_ = !polyphase_;
        select(probe_);
        return true;
    }
    if (e == Event::Character('w') || e == Event::Character('W')) {
        bank_ = (bank_ + (e == Event::Character('w') ? 1 : kBanks.size() - 1)) % kBanks.size();
        return true;
    }
    if (e == Event::Character('i')) {
        file_ = !file_;
        return true;
    }
    if (e == Event::Character('+') || e == Event::Character('=')) {
        length_ = std::min(kLengths.size() - 1, length_ + 1);
        return true;
    }
    if (e == Event::Character('-')) {
        length_ = length_ ? length_ - 1 : 0;
        return true;
    }
    if (e == Event::Character('v')) {
        view_ = view_ == ProbeView::time ? ProbeView::spectrum : view_ == ProbeView::spectrum ? ProbeView::numbers : ProbeView::time;
        return true;
    }
    if (e == Event::ArrowLeft || e == Event::ArrowRight) {
        const auto all = wires();
        auto it = std::find(all.begin(), all.end(), probe_);
        const auto i = it == all.end() ? std::size_t{0} : static_cast<std::size_t>(it - all.begin());
        probe_ = all[(i + (e == Event::ArrowRight ? 1 : all.size() - 1)) % all.size()];
        return true;
    }
    return false;
}

std::vector<MenuItem> DiagramScreen::context_menu(int x, int y) {
    if (auto reset = zoom_.reset_item(x, y)) return {*reset};
    return {};
}

std::shared_ptr<TabScreen> make_diagram_screen(UiContext& ui, std::shared_ptr<DesignerModel> designer, std::shared_ptr<SignalModel> signal) {
    return std::make_shared<DiagramScreen>(ui, std::move(designer), std::move(signal));
}

}  // namespace signals::tui
