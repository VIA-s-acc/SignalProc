#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "pipeline/block.hpp"

namespace signals::pipeline::blocks {

using Kind = ParamSpec::Kind;
using Out = core::Result<std::vector<Value>>;
using Samples = std::vector<double>;

inline const std::vector<PortSpec> kIn{{"in", PortType::signal}};
inline const std::vector<PortSpec> kOut{{"out", PortType::signal}};
inline const std::vector<PortSpec> kCoeffsIn{{"in", PortType::coeffs}};
inline const std::vector<PortSpec> kCoeffsOut{{"coeffs", PortType::coeffs}};
inline const std::vector<PortSpec> kReportOut{{"report", PortType::report}};
inline const std::vector<PortSpec> kPair{{"a", PortType::signal}, {"b", PortType::signal}};
inline const ParamSpec kRate{"rate", Kind::integer, "44100", {}, 1, 768000};
inline const ParamSpec kDuration{"duration", Kind::real, "1", {}, 0, 3600};
inline const ParamSpec kAmplitude{"amplitude", Kind::real, "1", {}, -1e6, 1e6};

inline const core::AudioBuffer& audio(std::span<const Value> in, std::size_t i = 0) { return std::get<core::AudioBuffer>(in[i]); }
inline const Coeffs& coeffs(std::span<const Value> in) { return std::get<Coeffs>(in[0]); }

template <class... A>
std::unexpected<core::Error> fail(const std::string& text, A&&... args) {
    return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(text), std::forward<A>(args)...));
}

inline Out one(Value v) { return std::vector<Value>{std::move(v)}; }

template <class F>
Out each_channel(const core::AudioBuffer& a, F&& f) {
    core::AudioBuffer out;
    out.sample_rate = a.sample_rate;
    for (const auto& c : a.channels) {
        if constexpr (std::is_same_v<std::invoke_result_t<F&, const Samples&>, Samples>) {
            out.channels.push_back(f(c));
        } else {
            auto r = f(c);
            if (!r) return std::unexpected{r.error()};
            out.channels.push_back(std::move(*r));
        }
    }
    return one(std::move(out));
}

template <class F>
Out each_sample(std::span<const Value> in, F f) {
    return each_channel(audio(in), [&](const Samples& c) {
        Samples y(c.size());
        std::transform(c.begin(), c.end(), y.begin(), f);
        return y;
    });
}

inline std::optional<core::Error> same_rate(std::string_view block, const core::AudioBuffer& a, const core::AudioBuffer& b) {
    if (a.sample_rate == b.sample_rate) return std::nullopt;
    return core::Error{core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("pipeline.error.rates")), fmt::arg("block", block), fmt::arg("a", a.sample_rate),
                                                                 fmt::arg("b", b.sample_rate))};
}

inline const Samples* channel_or_last(const core::AudioBuffer& a, std::size_t ch) {
    return a.channels.empty() ? nullptr : &a.channels[std::min(ch, a.channels.size() - 1)];
}

Out pairwise(std::string_view block, const core::AudioBuffer& a, const core::AudioBuffer& b, auto&& combine) {
    if (auto e = same_rate(block, a, b)) return std::unexpected{*e};
    core::AudioBuffer out;
    out.sample_rate = a.sample_rate;
    for (std::size_t ch = 0; ch < std::max(a.channels.size(), b.channels.size()); ++ch) {
        static const Samples none;
        const auto* x = channel_or_last(a, ch);
        const auto* y = channel_or_last(b, ch);
        auto r = combine(x ? *x : none, y ? *y : none);
        if (!r) return std::unexpected{r.error()};
        out.channels.push_back(std::move(*r));
    }
    return one(std::move(out));
}

inline core::Result<std::vector<double>> parse_numbers(std::string_view block, const std::string& name, const std::string& text) {
    std::string spaced = text;
    std::ranges::replace(spaced, ',', ' ');
    std::istringstream in{spaced};
    in.imbue(std::locale::classic());
    std::vector<double> out;
    for (std::string token; in >> token;) {
        std::istringstream part{token};
        part.imbue(std::locale::classic());
        double v = 0;
        if (!(part >> v) || !part.eof() || !std::isfinite(v))
            return fail(tr("pipeline.error.numbers"), fmt::arg("block", block), fmt::arg("param", name), fmt::arg("value", text));
        out.push_back(v);
    }
    if (out.empty()) return fail(tr("pipeline.error.numbers"), fmt::arg("block", block), fmt::arg("param", name), fmt::arg("value", text));
    return out;
}

inline std::string_view inverse_of(std::string_view transform) {
    if (transform == "swt") return "iswt";
    if (transform == "lift53" || transform == "lift53i") return "ilift53";
    if (transform == "lift97") return "ilift97";
    if (transform == "wpt") return "iwpt";
    return "idwt";
}

inline bool finite(const Value& v) {
    if (const auto* a = std::get_if<core::AudioBuffer>(&v))
        return std::ranges::all_of(a->channels, [](const Samples& c) { return std::ranges::all_of(c, [](double x) { return std::isfinite(x); }); });
    if (const auto* r = std::get_if<Report>(&v))
        return std::ranges::all_of(r->fields, [](const auto& f) { return !std::holds_alternative<double>(f.second) || std::isfinite(std::get<double>(f.second)); });
    if (const auto* c = std::get_if<Coeffs>(&v))
        return std::ranges::all_of(c->channels, [](const auto& ch) {
            return std::ranges::all_of(ch.bands, [](const Samples& b) { return std::ranges::all_of(b, [](double x) { return std::isfinite(x); }); });
        });
    return true;
}

inline BlockType checked(BlockType t) {
    t.run = [run = std::move(t.run), id = t.id](const RunContext& ctx, std::span<const Value> in, const Params& p) -> Out {
        auto out = run(ctx, in, p);
        if (out && !std::ranges::all_of(*out, finite)) return fail(tr("pipeline.error.not_finite"), fmt::arg("block", id));
        return out;
    };
    return t;
}

}  // namespace signals::pipeline::blocks
