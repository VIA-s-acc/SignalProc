#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <utility>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline {
namespace {

const std::vector<BlockType>& registry() {
    static const std::vector<BlockType> all = [] {
        std::vector<BlockType> out;
        register_basic_blocks(out);
        register_dsp_blocks(out);
        register_level_blocks(out);
        register_source_blocks(out);
        register_filter_blocks(out);
        register_wavelet_blocks(out);
        register_channel_blocks(out);
        register_measure_blocks(out);
        register_restore_blocks(out);
        register_fx_blocks(out);
        register_util_blocks(out);
        register_design_blocks(out);
        register_coeff_blocks(out);
        register_effect_blocks(out);
        register_spectral_blocks(out);
        register_analysis_blocks(out);
        register_tempo_blocks(out);
        return out;
    }();
    return all;
}

bool parse_real(std::string_view s, double& out) {
    if (s.empty()) return false;
    std::istringstream in{std::string{s}};
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.peek() == std::char_traits<char>::eof() && std::isfinite(out);
}

bool parse_int(std::string_view s, long long& out) {
    const auto* end = s.data() + s.size();
    const auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc{} && r.ptr == end;
}

core::Error bad(std::string_view key, const ParamSpec& p, std::string_view value) {
    std::string choices;
    for (const auto& c : p.choices) choices += (choices.empty() ? "" : ", ") + c;
    return {core::Errc::invalid_argument, fmt::format(fmt::runtime(tr(key)), fmt::arg("param", p.name), fmt::arg("value", value), fmt::arg("choices", choices),
                                                      fmt::arg("min", p.min), fmt::arg("max", p.max))};
}

bool in_bounds(const ParamSpec& p, double v) { return v >= p.min && v <= p.max; }

bool band_name(std::string_view s) {
    if (s == "a") return true;
    if (s.size() < 2 || (s[0] != 'a' && s[0] != 'd')) return false;
    return std::all_of(s.begin() + 1, s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool gains_valid(std::string_view text) {
    const auto trim = [](std::string_view s) {
        while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
        while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
        return s;
    };
    for (std::size_t at = 0; at <= text.size();) {
        auto end = text.find(',', at);
        if (end == std::string_view::npos) end = text.size();
        const auto item = trim(text.substr(at, end - at));
        if (!item.empty() || end < text.size()) {
            const auto eq = item.find('=');
            double gain = 0;
            if (eq == std::string_view::npos || !band_name(trim(item.substr(0, eq))) || !parse_real(trim(item.substr(eq + 1)), gain)) return false;
        }
        at = end + 1;
    }
    return true;
}

}  // namespace

std::span<const BlockType> block_types() { return registry(); }

const BlockType* find_block_type(std::string_view id) {
    for (const auto& t : registry())
        if (t.id == id) return &t;
    return nullptr;
}

core::Result<Params> resolve_params(const BlockType& t, const Params& given) {
    for (const auto& [name, value] : given)
        if (std::none_of(t.params.begin(), t.params.end(), [&](const ParamSpec& p) { return p.name == name; }))
            return std::unexpected{core::Error{core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.unknown_param")),
                                                                                       fmt::arg("block", t.id), fmt::arg("param", name))}};
    Params out;
    for (const auto& p : t.params) {
        const auto it = given.find(p.name);
        const std::string value = it == given.end() ? p.fallback : it->second;
        double real = 0;
        long long integer = 0;
        switch (p.kind) {
        case ParamSpec::Kind::integer:
            if (!parse_int(value, integer)) return std::unexpected{bad("pipeline.error.bad_integer", p, value)};
            if (!in_bounds(p, static_cast<double>(integer))) return std::unexpected{bad("pipeline.error.out_of_bounds", p, value)};
            break;
        case ParamSpec::Kind::real:
            if (!parse_real(value, real)) return std::unexpected{bad("pipeline.error.bad_number", p, value)};
            if (!in_bounds(p, real)) return std::unexpected{bad("pipeline.error.out_of_bounds", p, value)};
            break;
        case ParamSpec::Kind::choice:
            if (std::find(p.choices.begin(), p.choices.end(), value) == p.choices.end()) return std::unexpected{bad("pipeline.error.bad_choice", p, value)};
            break;
        case ParamSpec::Kind::range: {
            const auto dots = value.find("..");
            long long a = 0, b = 0;
            const bool ok = dots == std::string::npos ? parse_int(value, a) : parse_int(std::string_view{value}.substr(0, dots), a) && parse_int(std::string_view{value}.substr(dots + 2), b);
            if (!ok || a < 0 || (dots != std::string::npos && b < a)) return std::unexpected{bad("pipeline.error.bad_range", p, value)};
            break;
        }
        case ParamSpec::Kind::samples: {
            const bool seconds = value.ends_with("s");
            const auto number = seconds ? std::string_view{value}.substr(0, value.size() - 1) : std::string_view{value};
            if (seconds ? !parse_real(number, real) || real < 0 : !parse_int(number, integer) || integer < 0)
                return std::unexpected{bad("pipeline.error.bad_samples", p, value)};
            if (!seconds && static_cast<double>(integer) > kMaxSamples) return std::unexpected{bad("pipeline.error.too_long", p, value)};
            break;
        }
        case ParamSpec::Kind::sigma:
            if (value != "mad_d1" && (!parse_real(value, real) || !(real > 0))) return std::unexpected{bad("pipeline.error.bad_sigma", p, value)};
            break;
        case ParamSpec::Kind::gains:
            if (!gains_valid(value)) return std::unexpected{bad("pipeline.error.bad_gains", p, value)};
            break;
        case ParamSpec::Kind::text:
        case ParamSpec::Kind::path: break;
        }
        out.emplace(p.name, value);
    }
    return out;
}

const std::string& param_text(const Params& p, std::string_view name) { return p.find(name)->second; }

double param_real(const Params& p, std::string_view name) {
    double v = 0;
    parse_real(param_text(p, name), v);
    return v;
}

long long param_int(const Params& p, std::string_view name) {
    long long v = 0;
    const auto& s = param_text(p, name);
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

core::Result<std::size_t> param_samples(const Params& p, std::string_view name, double rate) {
    const auto& s = param_text(p, name);
    if (!s.ends_with("s")) {
        const auto n = param_int(p, name);
        if (n < 0 || static_cast<double>(n) > kMaxSamples)
            return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.too_long")), fmt::arg("param", name), fmt::arg("value", s)));
        return static_cast<std::size_t>(n);
    }
    double seconds = 0;
    parse_real(std::string_view{s}.substr(0, s.size() - 1), seconds);
    if (!(rate > 0)) return core::make_error(core::Errc::invalid_argument, "seconds need a sample rate");
    if (!(seconds * rate <= kMaxSamples))
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.too_long")), fmt::arg("param", name), fmt::arg("value", s)));
    return static_cast<std::size_t>(std::llround(seconds * rate));
}

std::pair<std::size_t, std::size_t> param_range(const Params& p, std::string_view name) {
    const auto& s = param_text(p, name);
    const auto dots = s.find("..");
    long long a = 0, b = 0;
    std::from_chars(s.data(), s.data() + (dots == std::string::npos ? s.size() : dots), a);
    if (dots == std::string::npos) return {static_cast<std::size_t>(a), static_cast<std::size_t>(a)};
    std::from_chars(s.data() + dots + 2, s.data() + s.size(), b);
    return {static_cast<std::size_t>(a), static_cast<std::size_t>(b)};
}

core::Result<std::size_t> checked_length(std::size_t n, std::size_t factor) {
    const auto cap = static_cast<std::size_t>(kMaxSamples);
    if (factor != 0 && n > cap / factor)
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.output_too_long")), fmt::arg("n", n), fmt::arg("factor", factor)));
    return n * factor;
}

}  // namespace signals::pipeline
