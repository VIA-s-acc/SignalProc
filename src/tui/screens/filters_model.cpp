#include "tui/screens/filters_model.hpp"
#include "tui/data/user_filters.hpp"
#include "core/wavelet/regularity.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <locale>
#include <new>
#include <numbers>
#include <sstream>

#include <fmt/format.h>

#include "core/bank.hpp"
#include "core/design/families.hpp"
#include "core/design/fir.hpp"
#include "core/design/window_fir.hpp"
#include "core/packets.hpp"
#include "core/wavelet/spectra.hpp"
#include "i18n/i18n.hpp"
#include "tui/shell/errors.hpp"
#include "tui/widgets/format.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr double kLinearPhaseTol = 1e-9;
constexpr double kOrthogonalityTol = 1e-12;

std::string_view role_name(FilterRole r) {
    switch (r) {
    case FilterRole::h0: return "h₀";
    case FilterRole::h1: return "h₁";
    case FilterRole::f0: return "f₀";
    case FilterRole::f1: return "f₁";
    }
    return "";
}

const core::Laurent& role_filter(const core::FilterBank& b, FilterRole r) {
    switch (r) {
    case FilterRole::h0: return b.h0;
    case FilterRole::h1: return b.h1;
    case FilterRole::f0: return b.f0;
    case FilterRole::f1: return b.f1;
    }
    return b.h0;
}

bool analysis_side(FilterRole r) { return r == FilterRole::h0 || r == FilterRole::h1; }

std::string factors_suffix(std::size_t factors) { return factors ? " · J=" + std::to_string(factors) : std::string{}; }

}  // namespace

std::string ClassicalSpec::key() const {
    static constexpr std::array<const char*, 5> methods{"iir", "remez", "firls", "window", "freqsamp"};
    std::string out = methods[static_cast<std::size_t>(method)];
    static constexpr std::array<const char*, 5> families{"butter", "cheby1", "cheby2", "ellip", "bessel"};
    static constexpr std::array<const char*, 4> types{"lowpass", "highpass", "bandpass", "bandstop"};
    const bool windowed = method == Method::window || method == Method::freqsamp;
    out += "/" + (windowed ? std::string{core::fir_window_name(window)} : std::string{families[static_cast<std::size_t>(family)]}) + "/" +
           types[static_cast<std::size_t>(type)] + "/" + std::to_string(order);
    for (const double e : edges) out += "/" + fmt::format("{:.6g}", e);
    out += "/rp" + fmt::format("{:.6g}", rp) + "/rs" + fmt::format("{:.6g}", rs);
    return out;
}

std::string ClassicalSpec::label() const {
    static constexpr std::array<const char*, 5> families{"tui.filters.classic.butter", "tui.filters.classic.cheby1", "tui.filters.classic.cheby2",
                                                         "tui.filters.classic.ellip", "tui.filters.classic.bessel"};
    static constexpr std::array<const char*, 4> types{"tui.filters.classic.lowpass", "tui.filters.classic.highpass", "tui.filters.classic.bandpass",
                                                      "tui.filters.classic.bandstop"};
    std::string name;
    switch (method) {
    case Method::iir: name = tr(families[static_cast<std::size_t>(family)]); break;
    case Method::remez: name = tr("tui.filters.classic.remez"); break;
    case Method::firls: name = tr("tui.filters.classic.firls"); break;
    case Method::window: name = tr("tui.filters.classic.window." + std::string{core::fir_window_name(window)}); break;
    case Method::freqsamp: name = tr("tui.filters.classic.freqsamp"); break;
    }
    std::string range;
    for (std::size_t i = 0; i < edges.size(); ++i) range += (i ? "–" : "") + format_fixed(edges[i], 2);
    return name + " " + tr(types[static_cast<std::size_t>(type)]) + " " + std::to_string(order) + " · " + range + "π";
}

std::optional<ClassicalSpec> parse_classical(std::string_view key) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream in{std::string{key}};
    while (std::getline(in, part, '/')) parts.push_back(part);
    if (parts.size() < 7) return std::nullopt;
    ClassicalSpec s;
    if (parts[0] == "iir") s.method = ClassicalSpec::Method::iir;
    else if (parts[0] == "remez") s.method = ClassicalSpec::Method::remez;
    else if (parts[0] == "firls") s.method = ClassicalSpec::Method::firls;
    else if (parts[0] == "window") s.method = ClassicalSpec::Method::window;
    else if (parts[0] == "freqsamp") s.method = ClassicalSpec::Method::freqsamp;
    else return std::nullopt;
    const auto type = core::parse_band_type(parts[2]);
    if (!type) return std::nullopt;
    if (s.method == ClassicalSpec::Method::window || s.method == ClassicalSpec::Method::freqsamp) {
        const auto window = core::parse_fir_window(parts[1]);
        if (!window) return std::nullopt;
        s.window = *window;
    } else {
        const auto family = core::parse_iir_family(parts[1]);
        if (!family) return std::nullopt;
        s.family = *family;
    }
    s.type = *type;
    s.edges.clear();
    try {
        s.order = std::stoi(parts[3]);
        for (std::size_t i = 4; i < parts.size(); ++i) {
            if (parts[i].starts_with("rp")) s.rp = std::stod(parts[i].substr(2));
            else if (parts[i].starts_with("rs")) s.rs = std::stod(parts[i].substr(2));
            else s.edges.push_back(std::stod(parts[i]));
        }
    } catch (...) {
        return std::nullopt;
    }
    if (s.edges.empty()) return std::nullopt;
    return s;
}

FilterRef classical_ref(const ClassicalSpec& spec) { return FilterRef{FilterRef::Kind::classical, spec.key(), FilterRole::h0}; }

std::optional<FilterRef> classical_step(const FilterRef& ref, int order_step, double edge_factor) {
    if (ref.kind != FilterRef::Kind::classical) return std::nullopt;
    auto s = parse_classical(ref.wavelet);
    if (!s) return std::nullopt;
    const bool fir = s->method != ClassicalSpec::Method::iir;
    s->order = std::clamp(s->order + order_step * (fir ? 2 : 1), fir ? 5 : 1, fir ? 255 : 20);
    if (fir && s->order % 2 == 0) ++s->order;
    const auto [low, high] = std::minmax_element(s->edges.begin(), s->edges.end());
    const double factor = std::clamp(edge_factor, 0.01 / *low, 0.98 / *high);
    for (auto& e : s->edges) e *= factor;
    return classical_ref(*s);
}

std::optional<FilterRef> spectrum_step(const FilterRef& ref, int step) {
    if (ref.kind != FilterRef::Kind::scaling && ref.kind != FilterRef::Kind::wavelet_fn) return std::nullopt;
    constexpr std::size_t kMostFactors = 12;
    auto next = ref;
    if (step < 0) next.level = ref.level == 0 ? kMostFactors : std::max<std::size_t>(1, ref.level - 1);
    else if (step > 0 && ref.level != 0) next.level = ref.level + 1 > kMostFactors ? 0 : ref.level + 1;
    return next;
}

FilterRef custom_ref(const core::Laurent& h, std::string title) {
    std::string key = h.offset != 0 ? fmt::format("@{}:", h.offset) : std::string{};
    for (std::size_t k = 0; k < h.coeffs.size(); ++k) key += (k ? "," : "") + fmt::format("{:.17g}", h.coeffs[k]);
    FilterRef r{FilterRef::Kind::custom, std::move(key), FilterRole::h0};
    r.title = std::move(title);
    return r;
}

FilterRef zpk_ref(const core::Zpk& zpk, std::string title) {
    const auto list = [](const std::vector<std::complex<double>>& roots) {
        std::string out;
        for (const auto& r : roots) out += (out.empty() ? "" : ";") + fmt::format("{:.17g},{:.17g}", r.real(), r.imag());
        return out;
    };
    FilterRef r{FilterRef::Kind::zpk, fmt::format("k={:.17g}|z={}|p={}", zpk.k, list(zpk.z), list(zpk.p)), FilterRole::h0};
    r.title = std::move(title);
    return r;
}

std::optional<core::Zpk> parse_zpk(std::string_view key) {
    const auto number = [](std::string_view text) -> std::optional<double> {
        std::istringstream in{std::string{text}};
        in.imbue(std::locale::classic());
        double v = 0;
        if (!(in >> v) || in.peek() != std::char_traits<char>::eof() || !std::isfinite(v)) return std::nullopt;
        return v;
    };
    const auto roots = [&](std::string_view text) -> std::optional<std::vector<std::complex<double>>> {
        std::vector<std::complex<double>> out;
        while (!text.empty()) {
            const auto end = std::min(text.find(';'), text.size());
            const auto item = text.substr(0, end);
            const auto comma = item.find(',');
            if (comma == std::string_view::npos) return std::nullopt;
            const auto re = number(item.substr(0, comma)), im = number(item.substr(comma + 1));
            if (!re || !im) return std::nullopt;
            out.emplace_back(*re, *im);
            text.remove_prefix(std::min(end + 1, text.size()));
        }
        return out;
    };
    const auto k_at = key.find("k="), z_at = key.find("|z="), p_at = key.find("|p=");
    if (k_at != 0 || z_at == std::string_view::npos || p_at == std::string_view::npos || p_at < z_at) return std::nullopt;
    const auto k = number(key.substr(2, z_at - 2));
    auto z = roots(key.substr(z_at + 3, p_at - z_at - 3));
    auto p = roots(key.substr(p_at + 3));
    if (!k || !z || !p) return std::nullopt;
    return core::Zpk{std::move(*z), std::move(*p), *k};
}

namespace {

std::optional<double> plain_number(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::istringstream in{std::string{text}};
    in.imbue(std::locale::classic());
    double v = 0;
    if (!(in >> v) || in.peek() != std::char_traits<char>::eof() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}

std::optional<std::vector<std::complex<double>>> parse_root(std::string token) {
    token = replace_all(replace_all(replace_all(std::move(token), "∠", "@"), "±", "~"), "π", "pi");
    std::vector<std::complex<double>> out;
    if (const auto at = token.find('@'); at != std::string::npos) {
        const auto r = plain_number(std::string_view{token}.substr(0, at));
        auto angle = token.substr(at + 1);
        const bool pair = angle.starts_with('~');
        if (pair) angle.erase(0, 1);
        if (angle.ends_with("pi")) angle.resize(angle.size() - 2);
        const auto a = plain_number(angle);
        if (!r || !a) return std::nullopt;
        out.push_back(std::polar(*r, *a * std::numbers::pi));
        if (pair) out.push_back(std::conj(out.back()));
        return out;
    }
    if (token.ends_with('i') || token.ends_with('j')) {
        const std::string body = token.substr(0, token.size() - 1);
        std::size_t split = std::string::npos;
        for (std::size_t k = body.size(); k-- > 1;)
            if ((body[k] == '+' || body[k] == '-' || body[k] == '~') && body[k - 1] != 'e' && body[k - 1] != 'E') {
                split = k;
                break;
            }
        const auto imaginary = [](std::string_view s) -> std::optional<double> {
            if (s.empty() || s == "+") return 1.0;
            if (s == "-") return -1.0;
            return plain_number(s);
        };
        double re = 0;
        std::optional<double> im;
        bool pair = false;
        if (split == std::string::npos) {
            im = imaginary(body);
        } else {
            const auto real = plain_number(std::string_view{body}.substr(0, split));
            if (!real) return std::nullopt;
            re = *real;
            pair = body[split] == '~';
            im = imaginary(pair ? std::string_view{body}.substr(split + 1) : std::string_view{body}.substr(split));
            if (pair && split + 1 == body.size()) return std::nullopt;
        }
        if (!im) return std::nullopt;
        out.emplace_back(re, *im);
        if (pair) out.push_back(std::conj(out.back()));
        return out;
    }
    const auto real = plain_number(token);
    if (!real) return std::nullopt;
    out.emplace_back(*real, 0.0);
    return out;
}

}  // namespace

std::optional<std::vector<std::complex<double>>> parse_roots(std::string_view text) {
    std::vector<std::complex<double>> out;
    std::string token;
    const auto flush = [&]() -> bool {
        if (token.empty()) return true;
        auto roots = parse_root(std::move(token));
        token.clear();
        if (!roots) return false;
        out.insert(out.end(), roots->begin(), roots->end());
        return true;
    };
    for (const char c : text) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t') {
            if (!flush()) return std::nullopt;
        } else {
            token += c;
        }
    }
    if (!flush()) return std::nullopt;
    return out;
}

std::string roots_text(const std::vector<std::complex<double>>& roots) {
    std::vector<bool> used(roots.size(), false);
    std::string out;
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (used[i]) continue;
        const auto r = roots[i];
        const double size = std::max(1.0, std::abs(r));
        std::string item;
        if (std::abs(r.imag()) <= 1e-12 * size) {
            item = fmt::format("{:.10g}", r.real());
        } else {
            std::size_t mate = roots.size();
            for (std::size_t j = i + 1; j < roots.size(); ++j)
                if (!used[j] && std::abs(roots[j] - std::conj(r)) < 1e-9 * size) {
                    mate = j;
                    break;
                }
            if (mate < roots.size()) {
                used[mate] = true;
                item = fmt::format("{:.10g}∠±{:.10g}π", std::abs(r), std::abs(std::arg(r)) / std::numbers::pi);
            } else {
                item = fmt::format("{:.10g}{}{:.10g}i", r.real(), r.imag() < 0 ? "-" : "+", std::abs(r.imag()));
            }
        }
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

std::optional<core::Zpk> editable_zpk(const FilterAnalysis& a) {
    if (a.zpk) return *a.zpk;
    if (!a.zp) return std::nullopt;
    const auto first = std::find_if(a.h.coeffs.begin(), a.h.coeffs.end(), [](double v) { return v != 0.0; });
    if (first == a.h.coeffs.end()) return std::nullopt;
    core::Zpk out{a.zp->zeros, a.zp->poles, *first};
    for (std::ptrdiff_t k = 0; k < a.zp->poles_at_origin; ++k) out.p.emplace_back(0.0, 0.0);
    for (std::ptrdiff_t k = 0; k < -a.zp->poles_at_origin; ++k) out.z.emplace_back(0.0, 0.0);
    return out;
}

core::Zpk move_root(core::Zpk zpk, bool pole, std::size_t index, std::complex<double> to) {
    auto& roots = pole ? zpk.p : zpk.z;
    if (index >= roots.size()) return zpk;
    const auto from = roots[index];
    const double size = std::max(1.0, std::abs(from));
    if (std::abs(from.imag()) <= 1e-12 * size) {
        std::size_t mate = roots.size();
        for (std::size_t j = 0; j < roots.size(); ++j)
            if (j != index && std::abs(roots[j] - from) < 1e-9 * size) {
                mate = j;
                break;
            }
        if (mate == roots.size() || to.imag() == 0.0) {
            roots[index] = {to.real(), 0.0};
            return zpk;
        }
        roots[index] = to;
        roots[mate] = std::conj(to);
        return zpk;
    }
    std::size_t mate = index;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t j = 0; j < roots.size(); ++j)
        if (j != index && std::abs(roots[j] - std::conj(from)) < best) best = std::abs(roots[j] - std::conj(from)), mate = j;
    roots[index] = to;
    if (mate != index) roots[mate] = std::conj(to);
    return zpk;
}

double span_of(const FilterRef& ref) { return ref.kind == FilterRef::Kind::scaling || ref.kind == FilterRef::Kind::wavelet_fn ? 8.0 : 1.0; }

std::string FilterRef::label() const {
    if (kind == Kind::classical) {
        const auto s = parse_classical(wavelet);
        return s ? s->label() : wavelet;
    }
    switch (kind) {
    case Kind::bank:
    case Kind::user: return std::string{role_name(role)} + " " + wavelet;
    case Kind::equivalent: return (detail ? "d" : "a") + subscript_count(level) + " " + wavelet;
    case Kind::distortion: return "T(z) " + wavelet;
    case Kind::alias: return "A(z) " + wavelet;
    case Kind::scaling: return std::string{analysis_side(role) ? "Φ̃ " : "Φ "} + wavelet + factors_suffix(level);
    case Kind::wavelet_fn: return std::string{analysis_side(role) ? "Ψ̃ " : "Ψ "} + wavelet + factors_suffix(level);
    case Kind::cwt: return "Ψ̂ " + wavelet + " a=" + std::to_string(level);
    case Kind::packet: return core::packet_path({level, node}) + " " + wavelet;
    case Kind::custom: {
        if (!title.empty()) return title;
        const auto taps = static_cast<std::size_t>(std::count(wavelet.begin(), wavelet.end(), ',')) + 1;
        return fmt::format(fmt::runtime(tr("tui.filters.custom.label")), fmt::arg("taps", tr_count("unit.tap", static_cast<long long>(taps))));
    }
    case Kind::zpk: return title.empty() ? tr("tui.filters.zpk.label") : title;
    case Kind::classical: break;
    }
    return wavelet;
}

LibraryNode build_library(std::string_view equivalent_wavelet, std::size_t levels) {
    LibraryNode root{tr("tui.filters.library")};
    LibraryNode wavelets{tr("tui.filters.group.wavelets")};
    for (auto name : core::wavelet_names()) {
        const auto info = core::wavelet_info(name);
        const std::string family = info ? info->family : std::string{"?"};
        auto it = std::find_if(wavelets.children.begin(), wavelets.children.end(), [&](const auto& n) { return n.label == family; });
        if (it == wavelets.children.end()) it = wavelets.children.insert(wavelets.children.end(), LibraryNode{family});
        it->children.push_back({std::string{name}, {}, FilterRef{FilterRef::Kind::bank, std::string{name}, FilterRole::h0}});
    }
    const std::string eq_name{equivalent_wavelet};
    LibraryNode equivalent{fmt::format(fmt::runtime(tr("tui.filters.group.equivalent")), fmt::arg("wavelet", eq_name), fmt::arg("levels", levels))};
    FilterRef approx{FilterRef::Kind::equivalent, eq_name, FilterRole::h0, levels, false};
    equivalent.children.push_back({approx.label(), {}, approx});
    for (std::size_t j = levels; j >= 1; --j) {
        FilterRef d{FilterRef::Kind::equivalent, eq_name, FilterRole::h0, j, true};
        equivalent.children.push_back({d.label(), {}, d});
    }
    LibraryNode chain{tr("tui.filters.group.chain")};
    for (auto kind : {FilterRef::Kind::distortion, FilterRef::Kind::alias}) {
        FilterRef r{kind, eq_name};
        chain.children.push_back({r.label(), {}, r});
    }
    root.children.push_back(std::move(wavelets));
    LibraryNode classic{tr("tui.filters.group.classic")};
    using M = ClassicalSpec::Method;
    using F = core::IirFamily;
    using T = core::BandType;
    for (const auto& spec : {ClassicalSpec{M::iir, F::butterworth, T::lowpass, 4, {0.25}}, ClassicalSpec{M::iir, F::chebyshev1, T::lowpass, 4, {0.25}, 1, 40},
                             ClassicalSpec{M::iir, F::chebyshev2, T::lowpass, 4, {0.25}, 1, 40}, ClassicalSpec{M::iir, F::elliptic, T::lowpass, 4, {0.25}, 1, 40},
                             ClassicalSpec{M::iir, F::bessel, T::lowpass, 4, {0.25}}, ClassicalSpec{M::iir, F::butterworth, T::highpass, 4, {0.5}},
                             ClassicalSpec{M::iir, F::butterworth, T::bandpass, 4, {0.2, 0.4}}, ClassicalSpec{M::iir, F::butterworth, T::bandstop, 4, {0.3, 0.5}},
                             ClassicalSpec{M::remez, F::butterworth, T::lowpass, 31, {0.4, 0.5}}, ClassicalSpec{M::firls, F::butterworth, T::lowpass, 31, {0.4, 0.5}},
                             ClassicalSpec{M::window, F::butterworth, T::lowpass, 31, {0.25}, 1, 40, core::FirWindow::hamming},
                             ClassicalSpec{M::window, F::butterworth, T::highpass, 41, {0.5}, 1, 60, core::FirWindow::kaiser},
                             ClassicalSpec{M::window, F::butterworth, T::bandpass, 51, {0.2, 0.4}, 1, 40, core::FirWindow::blackman},
                             ClassicalSpec{M::freqsamp, F::butterworth, T::lowpass, 33, {0.4, 0.5}, 1, 40, core::FirWindow::hamming}}) {
        const auto ref = classical_ref(spec);
        classic.children.push_back({ref.label(), {}, ref});
    }
    LibraryNode packets{fmt::format(fmt::runtime(tr("tui.filters.group.packets")), fmt::arg("wavelet", eq_name), fmt::arg("levels", levels))};
    for (std::size_t j = 1; j <= levels; ++j)
        for (std::size_t f = 0; f < (std::size_t{1} << j); ++f) {
            const auto n = core::natural_index_at(j, f);
            packets.children.push_back({core::packet_path({j, n}), {}, FilterRef{FilterRef::Kind::packet, eq_name, FilterRole::h0, j, false, n}});
        }
    packets.folded = true;
    LibraryNode cwt{tr("tui.filters.group.cwt")};
    cwt.folded = true;
    for (const char* name : {"morl", "mexh", "cmor1.5-1.0"})
        for (const std::size_t a : {2, 4, 8, 16, 32}) {
            const FilterRef r{FilterRef::Kind::cwt, name, FilterRole::h0, a};
            cwt.children.push_back({r.label(), {}, r});
        }
    LibraryNode spectra{fmt::format(fmt::runtime(tr("tui.filters.group.spectra")), fmt::arg("wavelet", eq_name))};
    spectra.children.push_back({"Φ", {}, FilterRef{FilterRef::Kind::scaling, eq_name, FilterRole::f0}});
    spectra.children.push_back({"Ψ", {}, FilterRef{FilterRef::Kind::wavelet_fn, eq_name, FilterRole::f0}});
    spectra.children.push_back({"Φ̃", {}, FilterRef{FilterRef::Kind::scaling, eq_name, FilterRole::h0}});
    spectra.children.push_back({"Ψ̃", {}, FilterRef{FilterRef::Kind::wavelet_fn, eq_name, FilterRole::h0}});
    spectra.folded = true;
    classic.folded = true;
    root.children.push_back(std::move(classic));
    root.children.push_back(std::move(equivalent));
    root.children.push_back(std::move(packets));
    root.children.push_back(std::move(chain));
    root.children.push_back(std::move(cwt));
    root.children.push_back(std::move(spectra));
    LibraryNode mine{tr("tui.filters.group.mine")};
    for (const auto& b : user_banks()) {
        LibraryNode node{b.name};
        for (auto role : {FilterRole::h0, FilterRole::h1, FilterRole::f0, FilterRole::f1}) {
            FilterRef r{FilterRef::Kind::user, b.name, role};
            node.children.push_back({r.label(), {}, r});
        }
        mine.children.push_back(std::move(node));
    }
    for (const auto& f : user_filters()) mine.children.push_back({f.name, {}, custom_ref(f.h, f.name)});
    if (mine.children.empty()) mine.later = tr("tui.filters.mine_empty");
    root.children.push_back(std::move(mine));
    return root;
}

core::Result<core::FilterBank> bank_of(const FilterRef& ref) {
    if (ref.kind != FilterRef::Kind::user) {
        auto bank = core::wavelet(ref.wavelet);
        if (bank || ref.kind == FilterRef::Kind::bank) return bank;
    }
    for (const auto& b : user_banks())
        if (b.name == ref.wavelet) return core::make_bank<double>(b.name, b.h0, b.f0, 1e-9);
    return core::make_error(core::Errc::invalid_argument, "no user bank named " + ref.wavelet);
}

namespace {

core::Result<FilterAnalysis> analyze_classical(const FilterRef& ref, std::size_t points) {
    const auto spec = parse_classical(ref.wavelet);
    if (!spec) return core::make_error(core::Errc::invalid_argument, "unknown classical filter " + ref.wavelet);
    FilterAnalysis a;
    a.ref = ref;
    const double edge = spec->edges.front();
    if (spec->method == ClassicalSpec::Method::iir) {
        core::IirSpec s{spec->family, spec->type, spec->order, spec->edges, spec->rp, spec->rs};
        auto zpk = core::iir_design(s);
        if (!zpk) return std::unexpected{zpk.error()};
        std::vector<double> omega(points);
        for (std::size_t k = 0; k < points; ++k) omega[k] = std::numbers::pi * static_cast<double>(k) / static_cast<double>(points);
        a.response = core::zpk_frequency_response(*zpk, omega);
        const auto sos = core::zpk_to_sos(*zpk);
        a.h = core::Laurent{core::sos_impulse(sos, 128), 0};
        a.zp = core::ZerosPoles{zpk->z, 0, zpk->p};
        a.stable = core::is_stable(*zpk);
        a.zpk = std::move(*zpk);
    } else {
        core::Result<std::vector<double>> h = core::make_error(core::Errc::invalid_argument, "fir");
        const double stop = spec->edges.size() > 1 ? spec->edges[1] : std::min(0.99, edge + 0.1);
        if (spec->method == ClassicalSpec::Method::remez) {
            core::RemezSpec r;
            r.numtaps = spec->order;
            r.bands = {0, edge / 2, stop / 2, 0.5};
            r.desired = {1, 0};
            h = core::remez(r);
        } else if (spec->method == ClassicalSpec::Method::window) {
            const bool pass_zero = spec->type == core::BandType::lowpass || spec->type == core::BandType::bandstop;
            h = core::firwin(spec->order, spec->edges, spec->window, pass_zero, spec->window == core::FirWindow::kaiser ? core::kaiser_beta(spec->rs) : 0.0);
        } else if (spec->method == ClassicalSpec::Method::freqsamp) {
            constexpr double kTransition = 0.05;
            const double hi = spec->edges.back();
            std::vector<double> f, g;
            switch (spec->type) {
            case core::BandType::lowpass: f = {0, edge, stop, 1}, g = {1, 1, 0, 0}; break;
            case core::BandType::highpass: f = {0, std::max(0.001, edge - kTransition), edge, 1}, g = {0, 0, 1, 1}; break;
            case core::BandType::bandpass:
            case core::BandType::bandstop: {
                f = {0, std::max(0.001, edge - kTransition), edge, hi, std::min(0.999, hi + kTransition), 1};
                g = spec->type == core::BandType::bandpass ? std::vector<double>{0, 0, 1, 1, 0, 0} : std::vector<double>{1, 1, 0, 0, 1, 1};
                break;
            }
            }
            h = core::firwin2(spec->order, f, g, spec->window);
        } else {
            h = core::firls(spec->order, std::vector<double>{0, edge, stop, 1}, std::vector<double>{1, 1, 0, 0}, std::vector<double>{});
        }
        if (!h) return std::unexpected{h.error()};
        a.h = core::Laurent{std::move(*h), 0};
        a.response = core::frequency_response(a.h, points);
        if (auto zp = core::zeros_poles(a.h)) a.zp = std::move(*zp);
        a.linear_phase = core::is_linear_phase(a.h, kLinearPhaseTol);
    }
    if (spec->type == core::BandType::lowpass) a.metrics = core::band_metrics(a.response, std::max(0.01, 0.8 * edge), std::min(0.99, spec->edges.size() > 1 ? spec->edges[1] : 1.3 * edge));
    else a.metrics = core::band_metrics(a.response, -1.0, 2.0);
    return a;
}

core::FrequencyResponse sampled_response(std::vector<double> omega, std::vector<std::complex<double>> h) {
    core::FrequencyResponse r;
    const auto n = omega.size();
    r.omega = std::move(omega);
    r.h = std::move(h);
    r.magnitude.resize(n);
    r.magnitude_db.resize(n);
    r.phase.resize(n);
    r.group_delay.assign(n, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t k = 0; k < n; ++k) {
        r.magnitude[k] = std::abs(r.h[k]);
        r.magnitude_db[k] = r.magnitude[k] > 0 ? std::max(-300.0, 20 * std::log10(r.magnitude[k])) : -300.0;
        r.phase[k] = std::arg(r.h[k]);
    }
    r.phase_unwrapped = core::unwrap_phase(r.phase);
    for (std::size_t k = 0; n > 1 && k < n; ++k) {
        const auto lo = k == 0 ? 0 : k - 1, hi = k + 1 == n ? k : k + 1;
        r.group_delay[k] = -(r.phase_unwrapped[hi] - r.phase_unwrapped[lo]) / (r.omega[hi] - r.omega[lo]);
    }
    return r;
}

std::optional<double> phi_partition(const core::Laurent& lowpass) {
    constexpr double pi = std::numbers::pi;
    constexpr int kShifts = 256, kBase = 16;
    std::vector<double> w;
    for (int b = 0; b < kBase; ++b)
        for (int k = -kShifts; k <= kShifts; ++k) w.push_back(2 * pi * (static_cast<double>(b) / kBase + k));
    const auto v = core::scaling_spectrum(lowpass, w);
    if (!v) return std::nullopt;
    double worst = 0;
    for (std::size_t b = 0; b < kBase; ++b) {
        double sum = 0;
        for (std::size_t k = 0; k < 2 * kShifts + 1; ++k) sum += std::norm((*v)[b * (2 * kShifts + 1) + k]);
        worst = std::max(worst, std::abs(sum - 1));
    }
    return worst;
}

std::optional<double> psi_partition(const core::Laurent& lowpass, const core::Laurent& highpass) {
    constexpr int kBase = 8, kLow = -40, kHigh = 24;
    std::vector<double> w;
    for (int b = 0; b < kBase; ++b)
        for (int j = kLow; j <= kHigh; ++j) w.push_back(std::ldexp(std::numbers::pi * (1 + static_cast<double>(b) / kBase), j));
    const auto v = core::wavelet_spectrum(lowpass, highpass, w);
    if (!v) return std::nullopt;
    double worst = 0;
    const std::size_t per = kHigh - kLow + 1;
    for (std::size_t b = 0; b < kBase; ++b) {
        double sum = 0;
        for (std::size_t j = 0; j < per; ++j) sum += std::norm((*v)[b * per + j]);
        worst = std::max(worst, std::abs(sum - 1));
    }
    return worst;
}

core::Result<FilterAnalysis> analyze_spectrum(const FilterRef& ref, const core::FilterBank& bank, std::size_t points) {
    constexpr double pi = std::numbers::pi;
    const double kSpan = span_of(ref);
    const bool dual = analysis_side(ref.role);
    const auto lowpass = dual ? core::reverse(bank.h0) : bank.f0;
    const auto highpass = dual ? core::reverse(bank.h1) : bank.f1;
    const std::size_t n = 4 * points;
    std::vector<double> omega(n);
    for (std::size_t k = 0; k < n; ++k) omega[k] = kSpan * pi * static_cast<double>(k) / static_cast<double>(n);
    const bool scaling = ref.kind == FilterRef::Kind::scaling;
    auto values = scaling ? core::scaling_spectrum(lowpass, omega, ref.level) : core::wavelet_spectrum(lowpass, highpass, omega, ref.level);
    if (!values) return std::unexpected{values.error()};
    FilterAnalysis a;
    a.ref = ref;
    a.span = kSpan;
    a.factors = ref.level > 0 ? ref.level : core::spectrum_factors(omega.back());
    a.response = sampled_response(std::move(omega), std::move(*values));
    a.metrics = core::band_metrics(a.response, -1.0, 2 * kSpan);
    a.zeros_at_minus_one = core::zeros_at_minus_one(scaling ? lowpass : (dual ? bank.f0 : bank.h0), 1e-6);
    const auto info = core::wavelet_info(ref.wavelet);
    const bool orthogonal = info ? info->orthogonal : core::check_orthogonality(bank, kOrthogonalityTol).passed();
    if (orthogonal && ref.level == 0) a.partition_error = scaling ? phi_partition(lowpass) : psi_partition(lowpass, highpass);
    return a;
}

core::Result<FilterAnalysis> analyze_cwt(const FilterRef& ref, std::size_t points) {
    constexpr double pi = std::numbers::pi;
    const auto cw = core::continuous_wavelet(ref.wavelet);
    if (!cw) return std::unexpected{cw.error()};
    if (ref.level == 0) return core::make_error(core::Errc::invalid_argument, "cwt spectrum: the scale must be positive");
    const double scale = static_cast<double>(ref.level);
    std::vector<double> omega(points), scaled(points);
    for (std::size_t k = 0; k < points; ++k) {
        omega[k] = pi * static_cast<double>(k) / static_cast<double>(points);
        scaled[k] = scale * omega[k];
    }
    FilterAnalysis a;
    a.ref = ref;
    a.response = sampled_response(std::move(omega), core::continuous_spectrum(*cw, scaled));
    a.metrics = core::band_metrics(a.response, -1.0, 2.0);
    const double guess = cw->family == core::ContinuousWavelet::Family::morl   ? 5.0
                         : cw->family == core::ContinuousWavelet::Family::mexh ? std::sqrt(2.0)
                                                                                : 2 * pi * cw->center;
    constexpr std::size_t kDense = 40000;
    std::vector<double> x(kDense + 1);
    for (std::size_t i = 0; i <= kDense; ++i) x[i] = (4 * guess + 10) * static_cast<double>(i) / kDense;
    const auto dense = core::continuous_spectrum(*cw, x);
    std::vector<double> mag(dense.size());
    for (std::size_t i = 0; i < dense.size(); ++i) mag[i] = std::abs(dense[i]);
    const auto peak = static_cast<std::size_t>(std::max_element(mag.begin(), mag.end()) - mag.begin());
    const double half = mag[peak] / std::sqrt(2.0);
    std::optional<double> lower, upper;
    for (std::size_t i = peak; i > 0 && !lower; --i)
        if (mag[i - 1] < half) lower = x[i - 1] + (half - mag[i - 1]) / (mag[i] - mag[i - 1]) * (x[i] - x[i - 1]);
    for (std::size_t i = peak; i + 1 < mag.size() && !upper; ++i)
        if (mag[i + 1] < half) upper = x[i] + (mag[i] - half) / (mag[i] - mag[i + 1]) * (x[i + 1] - x[i]);
    a.centre = x[peak] / (scale * pi);
    if (lower && upper) a.quality = x[peak] / (*upper - *lower);
    a.at_zero = mag.front();
    return a;
}

}  // namespace

namespace {

core::Result<FilterAnalysis> analyze_any(const FilterRef& ref, std::size_t points);

}  // namespace

core::Result<FilterAnalysis> analyze(const FilterRef& ref, std::size_t points) {
    auto a = analyze_any(ref, points);
    if (a) a->points = points;
    return a;
}

namespace {

core::Result<FilterAnalysis> analyze_custom(const FilterRef& ref, std::size_t points) {
    FilterAnalysis a;
    a.ref = ref;
    std::string_view taps = ref.wavelet;
    if (taps.starts_with('@')) {
        const auto colon = taps.find(':');
        if (colon == std::string_view::npos) return core::make_error(core::Errc::invalid_argument, "custom filter: bad offset in " + ref.wavelet);
        std::istringstream offset{std::string{taps.substr(1, colon - 1)}};
        offset.imbue(std::locale::classic());
        if (!(offset >> a.h.offset)) return core::make_error(core::Errc::invalid_argument, "custom filter: bad offset in " + ref.wavelet);
        taps.remove_prefix(colon + 1);
    }
    std::istringstream in{std::string{taps}};
    in.imbue(std::locale::classic());
    for (std::string part; std::getline(in, part, ',');) {
        std::istringstream value{part};
        value.imbue(std::locale::classic());
        double v = 0;
        if (!(value >> v)) return core::make_error(core::Errc::invalid_argument, "custom filter: bad coefficient " + part);
        a.h.coeffs.push_back(v);
    }
    if (a.h.coeffs.empty()) return core::make_error(core::Errc::invalid_argument, "custom filter: no coefficients");
    a.response = core::frequency_response(a.h, points);
    a.metrics = core::band_metrics(a.response, -1.0, 2.0);
    if (auto zp = core::zeros_poles(a.h)) a.zp = std::move(*zp);
    a.linear_phase = core::is_linear_phase(a.h, kLinearPhaseTol);
    return a;
}

core::Result<FilterAnalysis> analyze_zpk(const FilterRef& ref, std::size_t points) {
    const auto zpk = parse_zpk(ref.wavelet);
    if (!zpk) return core::make_error(core::Errc::invalid_argument, "zpk filter: cannot read " + ref.wavelet);
    FilterAnalysis a;
    a.ref = ref;
    std::vector<double> omega(points);
    for (std::size_t k = 0; k < points; ++k) omega[k] = std::numbers::pi * static_cast<double>(k) / static_cast<double>(points);
    a.response = core::zpk_frequency_response(*zpk, omega);
    auto square = *zpk;
    const auto size = std::max(square.z.size(), square.p.size());
    square.z.resize(size, {0.0, 0.0});
    square.p.resize(size, {0.0, 0.0});
    a.h = core::Laurent{core::sos_impulse(core::zpk_to_sos(square), 128), static_cast<std::ptrdiff_t>(zpk->p.size()) - static_cast<std::ptrdiff_t>(zpk->z.size())};
    a.zp = core::ZerosPoles{zpk->z, 0, zpk->p};
    a.stable = core::is_stable(*zpk);
    a.zpk = *zpk;
    a.metrics = core::band_metrics(a.response, -1.0, 2.0);
    return a;
}

core::Result<FilterAnalysis> analyze_any(const FilterRef& ref, std::size_t points) {
    if (ref.kind == FilterRef::Kind::classical) return analyze_classical(ref, points);
    if (ref.kind == FilterRef::Kind::zpk) return analyze_zpk(ref, points);
    if (ref.kind == FilterRef::Kind::custom) return analyze_custom(ref, points);
    if (ref.kind == FilterRef::Kind::cwt) return analyze_cwt(ref, points);
    const auto bank = bank_of(ref);
    if (!bank) return std::unexpected(bank.error());
    if (ref.kind == FilterRef::Kind::scaling || ref.kind == FilterRef::Kind::wavelet_fn) return analyze_spectrum(ref, *bank, points);
    FilterAnalysis a;
    a.ref = ref;
    double lo = 0, hi = 0.5;
    switch (ref.kind) {
    case FilterRef::Kind::bank:
    case FilterRef::Kind::user:
        a.h = role_filter(*bank, ref.role);
        if (ref.role == FilterRole::h1 || ref.role == FilterRole::f1) lo = 0.5, hi = 1.0;
        break;
    case FilterRef::Kind::equivalent: {
        auto eq = core::equivalent_filter(*bank, ref.level, ref.detail);
        if (!eq) return std::unexpected(eq.error());
        a.h = std::move(*eq);
        hi = std::ldexp(1.0, -static_cast<int>(ref.level) + (ref.detail ? 1 : 0));
        lo = ref.detail ? hi / 2 : 0.0;
        break;
    }
    case FilterRef::Kind::distortion: a.h = core::distortion_transfer(*bank), lo = -1; break;
    case FilterRef::Kind::alias: a.h = core::alias_transfer(*bank), lo = -1; break;
    case FilterRef::Kind::packet: {
        auto p = core::packet_filter(*bank, {ref.level, ref.node});
        if (!p) return std::unexpected(p.error());
        a.h = std::move(*p);
        const auto f = static_cast<double>(core::freq_position({ref.level, ref.node}));
        lo = std::ldexp(f, -static_cast<int>(ref.level));
        hi = std::ldexp(f + 1, -static_cast<int>(ref.level));
        break;
    }
    case FilterRef::Kind::classical:
    case FilterRef::Kind::custom:
    case FilterRef::Kind::zpk:
    case FilterRef::Kind::scaling:
    case FilterRef::Kind::wavelet_fn:
    case FilterRef::Kind::cwt: break;
    }
    a.response = core::frequency_response(a.h, points);
    a.metrics = lo == 0 ? core::band_metrics(a.response, hi / 2, std::min(1.0, 1.5 * hi)) : core::band_metrics(a.response, -1.0, 2.0);
    if (auto zp = core::zeros_poles(a.h)) a.zp = std::move(*zp);
    a.linear_phase = core::is_linear_phase(a.h, kLinearPhaseTol);
    const auto pr = core::check_pr(*bank);
    a.pr_error = std::max(pr.alias.value, pr.distortion.value);
    const auto info = ref.kind == FilterRef::Kind::user ? core::Result<core::WaveletInfo>{core::make_error(core::Errc::invalid_argument, "user")}
                                                        : core::wavelet_info(ref.wavelet);
    if (info && info->orthogonal) a.orth_error = core::check_orthogonality(*bank, kOrthogonalityTol).value;
    if (ref.kind == FilterRef::Kind::user) {
        const auto orth = core::check_orthogonality(*bank, kOrthogonalityTol);
        if (orth.passed()) a.orth_error = orth.value;
        a.sobolev = core::sobolev_exponent(analysis_side(ref.role) ? bank->h0 : bank->f0);
        return a;
    }
    if (const auto reg = core::wavelet_regularity(ref.wavelet)) {
        const bool synthesis = ref.kind == FilterRef::Kind::bank && !analysis_side(ref.role);
        a.sobolev = synthesis ? reg->sobolev_f0 : reg->sobolev_h0;
        if (ref.kind == FilterRef::Kind::bank && ref.role == FilterRole::h0) a.zeros_at_minus_one = reg->zeros_h0;
        if (ref.kind == FilterRef::Kind::bank && ref.role == FilterRole::f0) a.zeros_at_minus_one = reg->zeros_f0;
    }
    return a;
}

namespace {

void align_unwrapped(core::FrequencyResponse& r, const core::FrequencyResponse& full) {
    constexpr double pi = std::numbers::pi;
    if (full.omega.empty() || r.omega.empty()) return;
    const auto step = full.omega.size() > 1 ? full.omega[1] - full.omega[0] : pi;
    const auto i = std::min(full.omega.size() - 1, static_cast<std::size_t>(std::max(0.0, std::round(r.omega.front() / step))));
    const double shift = 2 * pi * std::round((full.phase_unwrapped[i] - r.phase_unwrapped.front()) / (2 * pi));
    for (auto& p : r.phase_unwrapped) p += shift;
}

}  // namespace

}  // namespace

core::FrequencyResponse range_response(const core::Laurent& h, double lo, double hi, std::size_t points, const core::FrequencyResponse* full) {
    constexpr double pi = std::numbers::pi;
    core::FrequencyResponse r;
    r.omega.resize(points);
    r.h.resize(points);
    r.magnitude.resize(points);
    r.magnitude_db.resize(points);
    r.phase.resize(points);
    r.group_delay.resize(points);
    std::vector<std::complex<double>> moment(points);
    for (std::size_t k = 0; k < points; ++k) {
        const double w = pi * (points > 1 ? lo + (hi - lo) * static_cast<double>(k) / static_cast<double>(points - 1) : lo);
        r.omega[k] = w;
        std::complex<double> sum{0, 0}, m{0, 0};
        for (std::size_t j = 0; j < h.coeffs.size(); ++j) {
            const auto n = static_cast<double>(h.offset + static_cast<std::ptrdiff_t>(j));
            const auto term = h.coeffs[j] * std::polar(1.0, -w * n);
            sum += term;
            m += n * term;
        }
        r.h[k] = sum;
        moment[k] = m;
        r.magnitude[k] = std::abs(sum);
        r.magnitude_db[k] = r.magnitude[k] > 0 ? std::max(-300.0, 20 * std::log10(r.magnitude[k])) : -300.0;
        r.phase[k] = std::arg(sum);
    }
    r.phase_unwrapped = r.phase;
    for (std::size_t k = 1; k < points; ++k) {
        double d = r.phase_unwrapped[k] - r.phase_unwrapped[k - 1];
        d -= 2 * pi * std::round(d / (2 * pi));
        r.phase_unwrapped[k] = r.phase_unwrapped[k - 1] + d;
    }
    if (full && points > 0) align_unwrapped(r, *full);
    const double peak = full && !full->magnitude.empty() ? *std::max_element(full->magnitude.begin(), full->magnitude.end())
                                                         : (points ? *std::max_element(r.magnitude.begin(), r.magnitude.end()) : 0.0);
    for (std::size_t k = 0; k < points; ++k)
        r.group_delay[k] = r.magnitude[k] > 1e-12 * peak ? (moment[k] / r.h[k]).real() : std::numeric_limits<double>::quiet_NaN();
    return r;
}

std::vector<double> masked_group_delay(const core::FrequencyResponse& r, double relative, std::optional<double> full_peak) {
    const double peak = full_peak ? *full_peak : r.magnitude.empty() ? 0.0 : *std::max_element(r.magnitude.begin(), r.magnitude.end());
    std::vector<double> out(r.group_delay);
    for (std::size_t k = 0; k < out.size(); ++k)
        if (r.magnitude[k] < relative * peak) out[k] = std::numeric_limits<double>::quiet_NaN();
    return out;
}

core::Result<void> ComparisonSet::add(FilterRef ref) {
    std::lock_guard lock{mutex_};
    if (std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.ref == ref; })) return {};
    if (entries_.size() >= kCapacity)
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("tui.filters.full")), fmt::arg("count", kCapacity)));
    entries_.push_back({std::move(ref)});
    return {};
}

core::Result<void> ComparisonSet::replace(std::size_t i, FilterRef ref) {
    std::lock_guard lock{mutex_};
    if (i >= entries_.size()) return core::make_error(core::Errc::invalid_argument, "replace: no such entry");
    if (std::any_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.ref == ref; })) return {};
    const bool visible = entries_[i].visible;
    entries_[i] = Entry{std::move(ref)};
    entries_[i].visible = visible;
    return {};
}

void ComparisonSet::remove(std::size_t i) {
    std::lock_guard lock{mutex_};
    if (i >= entries_.size()) return;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    if (reference_) {
        if (*reference_ == i) reference_.reset();
        else if (*reference_ > i) --*reference_;
    }
    if (focus_ > i || focus_ >= entries_.size()) focus_ = focus_ > 0 ? focus_ - 1 : 0;
}

void ComparisonSet::toggle(std::size_t i) {
    std::lock_guard lock{mutex_};
    if (i < entries_.size()) entries_[i].visible = !entries_[i].visible;
}

void ComparisonSet::set_reference(std::optional<std::size_t> i) {
    std::lock_guard lock{mutex_};
    reference_ = i && *i < entries_.size() ? i : std::nullopt;
}

void ComparisonSet::set_focus(std::size_t i) {
    std::lock_guard lock{mutex_};
    if (i < entries_.size()) focus_ = i;
}

std::vector<ComparisonSet::Entry> ComparisonSet::entries() const {
    std::lock_guard lock{mutex_};
    return entries_;
}

std::optional<std::size_t> ComparisonSet::reference() const {
    std::lock_guard lock{mutex_};
    return reference_;
}

std::size_t ComparisonSet::focus() const {
    std::lock_guard lock{mutex_};
    return focus_;
}

void ComparisonSet::store(const FilterRef& ref, std::shared_ptr<const FilterAnalysis> analysis, std::optional<std::string> error) {
    std::lock_guard lock{mutex_};
    std::erase(pending_, ref);
    for (auto& e : entries_)
        if (e.ref == ref) {
            e.analysis = std::move(analysis);
            e.error = std::move(error);
        }
}

void ComparisonSet::request(JobRunner& jobs, std::size_t points) {
    std::vector<FilterRef> todo;
    {
        std::lock_guard lock{mutex_};
        for (const auto& e : entries_)
            if (!e.error && (!e.analysis || e.analysis->points != points) &&
                std::find(pending_.begin(), pending_.end(), e.ref) == pending_.end())
                todo.push_back(e.ref);
        pending_.insert(pending_.end(), todo.begin(), todo.end());
    }
    if (todo.empty()) return;
    std::vector<std::string> stages;
    for (const auto& r : todo) stages.push_back(r.label());
    jobs.submit(tr("tui.filters.job"), std::move(stages), [this, todo, points](JobContext& ctx) {
        for (std::size_t i = 0; i < todo.size(); ++i) {
            if (ctx.cancelled()) {
                std::lock_guard lock{mutex_};
                for (std::size_t k = i; k < todo.size(); ++k) std::erase(pending_, todo[k]);
                return;
            }
            ctx.begin(i);
            try {
                auto a = analyze(todo[i], points);
                if (!a) {
                    const auto text = error_text(a.error());
                    store(todo[i], nullptr, text);
                    ctx.finish(0, text);
                    continue;
                }
                const auto bytes = a->h.coeffs.size() * sizeof(double) + a->response.omega.size() * 7 * sizeof(double);
                store(todo[i], std::make_shared<const FilterAnalysis>(std::move(*a)), std::nullopt);
                ctx.finish(bytes);
            } catch (const std::bad_alloc&) {
                store(todo[i], nullptr, tr("tui.jobs.out_of_memory"));
                ctx.finish(0, tr("tui.jobs.out_of_memory"));
            } catch (const std::exception& e) {
                const auto text = fmt::format(fmt::runtime(tr("tui.jobs.internal_error")), fmt::arg("what", e.what()));
                store(todo[i], nullptr, text);
                ctx.finish(0, text);
            }
        }
    });
}

void ComparisonSet::request_view(JobRunner& jobs, double lo, double hi, std::size_t points) {
    std::vector<std::pair<FilterRef, std::shared_ptr<const FilterAnalysis>>> todo;
    {
        std::lock_guard lock{mutex_};
        if (view_pending_) return;
        for (const auto& e : entries_)
            if (e.analysis && (e.analysis->zpk || !e.analysis->h.coeffs.empty()) &&
                (!e.view || e.view_lo != lo || e.view_hi != hi || e.view->omega.size() != points))
                todo.emplace_back(e.ref, e.analysis);
        if (todo.empty()) return;
        view_pending_ = true;
    }
    jobs.submit(tr("tui.filters.view_job"), {tr("tui.filters.view_stage")}, [this, todo, lo, hi, points](JobContext& ctx) {
        struct ClearPending {
            ComparisonSet& set;
            ~ClearPending() {
                std::lock_guard lock{set.mutex_};
                set.view_pending_ = false;
            }
        } clear{*this};
        ctx.begin(0);
        for (std::size_t i = 0; i < todo.size() && !ctx.cancelled(); ++i) {
            std::shared_ptr<const core::FrequencyResponse> r;
            if (todo[i].second->zpk) {
                std::vector<double> omega(points);
                for (std::size_t k = 0; k < points; ++k)
                    omega[k] = std::numbers::pi * (points > 1 ? lo + (hi - lo) * static_cast<double>(k) / static_cast<double>(points - 1) : lo);
                auto view = core::zpk_frequency_response(*todo[i].second->zpk, omega);
                align_unwrapped(view, todo[i].second->response);
                r = std::make_shared<const core::FrequencyResponse>(std::move(view));
            } else {
                r = std::make_shared<const core::FrequencyResponse>(range_response(todo[i].second->h, lo, hi, points, &todo[i].second->response));
            }
            std::lock_guard lock{mutex_};
            for (auto& e : entries_)
                if (e.ref == todo[i].first) e.view = r, e.view_lo = lo, e.view_hi = hi;
            ctx.report(static_cast<double>(i + 1) / static_cast<double>(todo.size()));
        }
        ctx.finish();
    });
}

std::optional<std::string> ComparisonSet::pending_label() const {
    std::lock_guard lock{mutex_};
    if (pending_.empty()) return std::nullopt;
    return pending_.front().label();
}

bool ComparisonSet::view_pending() const {
    std::lock_guard lock{mutex_};
    return view_pending_;
}

}  // namespace signals::tui
