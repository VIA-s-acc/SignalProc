#include <algorithm>
#include <cmath>

#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

bool whole(double v) { return v == std::floor(v); }

bool math_defined(std::string_view op, double x, double p) {
    if (op == "log" || op == "log10") return x > 0;
    if (op == "sqrt") return x >= 0;
    if (op == "reciprocal") return x != 0;
    if (op == "pow") return !(x < 0 && !whole(p)) && !(x == 0 && p < 0);
    return true;
}

double math_apply(std::string_view op, double x, double p) {
    if (op == "log") return std::log(x);
    if (op == "log10") return std::log10(x);
    if (op == "exp") return std::exp(x);
    if (op == "sqrt") return std::sqrt(x);
    if (op == "pow") return std::pow(x, p);
    if (op == "sign") return static_cast<double>((x > 0) - (x < 0));
    return 1 / x;
}

bool binary_defined(std::string_view op, double a, double b) {
    if (op == "divide") return b != 0;
    if (op == "pow") return math_defined("pow", a, b);
    return true;
}

double binary_apply(std::string_view op, double a, double b) {
    if (op == "divide") return a / b;
    if (op == "min") return std::min(a, b);
    if (op == "max") return std::max(a, b);
    if (op == "pow") return std::pow(a, b);
    return std::hypot(a, b);
}

std::optional<core::Error> need_stereo(std::string_view block, const core::AudioBuffer& a) {
    if (a.channels.size() >= 2) return std::nullopt;
    return core::Error{core::Errc::invalid_argument,
                       fmt::format(fmt::runtime(tr("pipeline.error.needs_stereo")), fmt::arg("block", block), fmt::arg("n", a.channels.size()))};
}

double sample_at(const Samples& c, std::size_t i) { return i < c.size() ? c[i] : 0.0; }

Samples shifted(const Samples& c, long long d) {
    Samples y(c.size(), 0.0);
    const auto n = static_cast<long long>(c.size());
    for (long long k = std::max(0LL, d); k < std::min(n, n + d); ++k) y[static_cast<std::size_t>(k)] = c[static_cast<std::size_t>(k - d)];
    return y;
}

}  // namespace

void register_util_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"affine",
                           kIn,
                           kOut,
                           {{"scale", Kind::real, "1", {}, -1e6, 1e6}, {"offset", Kind::real, "0", {}, -1e6, 1e6}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const double scale = param_real(p, "scale"), offset = param_real(p, "offset");
                               return each_sample(in, [scale, offset](double x) { return scale * x + offset; });
                           }}));
    out.push_back(checked({"math",
                           kIn,
                           kOut,
                           {{"op", Kind::choice, "log", {"log", "log10", "exp", "sqrt", "pow", "sign", "reciprocal"}}, {"p", Kind::real, "2", {}, -1000, 1000}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto& op = param_text(p, "op");
                               const double power = param_real(p, "p");
                               core::AudioBuffer result{a.sample_rate, {}};
                               for (std::size_t ch = 0; ch < a.channels.size(); ++ch) {
                                   const auto& c = a.channels[ch];
                                   Samples y(c.size());
                                   for (std::size_t i = 0; i < c.size(); ++i) {
                                       if (!math_defined(op, c[i], power))
                                           return fail(tr("pipeline.error.math_domain"), fmt::arg("op", op), fmt::arg("index", i), fmt::arg("channel", ch), fmt::arg("value", c[i]));
                                       y[i] = math_apply(op, c[i], power);
                                   }
                                   result.channels.push_back(std::move(y));
                               }
                               return one(std::move(result));
                           }}));
    out.push_back(checked({"binary",
                           kPair,
                           kOut,
                           {{"op", Kind::choice, "divide", {"divide", "min", "max", "pow", "hypot"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in, 0);
                               const auto& b = audio(in, 1);
                               if (auto e = same_rate("binary", a, b)) return std::unexpected{*e};
                               const auto& op = param_text(p, "op");
                               core::AudioBuffer result{a.sample_rate, {}};
                               static const Samples none;
                               for (std::size_t ch = 0; ch < std::max(a.channels.size(), b.channels.size()); ++ch) {
                                   const auto* x = channel_or_last(a, ch);
                                   const auto* y = channel_or_last(b, ch);
                                   const auto& xs = x ? *x : none;
                                   const auto& ys = y ? *y : none;
                                   Samples z(std::max(xs.size(), ys.size()));
                                   for (std::size_t i = 0; i < z.size(); ++i) {
                                       const double u = sample_at(xs, i), v = sample_at(ys, i);
                                       if (!binary_defined(op, u, v))
                                           return fail(tr("pipeline.error.binary_domain"), fmt::arg("op", op), fmt::arg("index", i), fmt::arg("channel", ch), fmt::arg("a", u),
                                                       fmt::arg("b", v));
                                       z[i] = binary_apply(op, u, v);
                                   }
                                   result.channels.push_back(std::move(z));
                               }
                               return one(std::move(result));
                           }}));
    out.push_back(checked({"repeat", kIn, kOut, {{"times", Kind::integer, "2", {}, 1, 100000}}, [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const auto times = static_cast<std::size_t>(param_int(p, "times"));
                               return each_channel(audio(in), [times](const Samples& c) -> core::Result<Samples> {
                                   const auto n = checked_length(c.size(), times);
                                   if (!n) return std::unexpected{n.error()};
                                   Samples y;
                                   y.reserve(*n);
                                   for (std::size_t t = 0; t < times; ++t) y.insert(y.end(), c.begin(), c.end());
                                   return y;
                               });
                           }}));
    out.push_back(checked({"select_channel", kIn, kOut, {{"index", Kind::integer, "0", {}, 0, 1023}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto index = static_cast<std::size_t>(param_int(p, "index"));
                               if (index >= a.channels.size()) return fail(tr("pipeline.error.select_channel"), fmt::arg("index", index), fmt::arg("n", a.channels.size()));
                               return one(core::AudioBuffer{a.sample_rate, {a.channels[index]}});
                           }}));
    out.push_back(checked({"channel_delay", kIn, kOut, {{"delays", Kind::text, "0"}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& text = param_text(p, "delays");
                               const auto delays = parse_numbers("channel_delay", "delays", text);
                               if (!delays) return std::unexpected{delays.error()};
                               if (!std::ranges::all_of(*delays, [](double d) { return whole(d) && std::abs(d) <= kMaxSamples; }))
                                   return fail(tr("pipeline.error.channel_delay"), fmt::arg("value", text));
                               const auto& a = audio(in);
                               core::AudioBuffer result{a.sample_rate, {}};
                               for (std::size_t ch = 0; ch < a.channels.size(); ++ch)
                                   result.channels.push_back(shifted(a.channels[ch], static_cast<long long>((*delays)[std::min(ch, delays->size() - 1)])));
                               return one(std::move(result));
                           }}));
    out.push_back(checked({"stereo_width", kIn, kOut, {{"width", Kind::real, "1.5", {}, 0, 2}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               auto a = audio(in);
                               if (auto e = need_stereo("stereo_width", a)) return std::unexpected{*e};
                               const double width = param_real(p, "width");
                               const auto n = std::max(a.channels[0].size(), a.channels[1].size());
                               Samples l(n), r(n);
                               for (std::size_t i = 0; i < n; ++i) {
                                   const double x = sample_at(a.channels[0], i), y = sample_at(a.channels[1], i);
                                   const double mid = (x + y) / 2, side = (x - y) / 2;
                                   l[i] = mid + width * side;
                                   r[i] = mid - width * side;
                               }
                               a.channels[0] = std::move(l);
                               a.channels[1] = std::move(r);
                               return one(std::move(a));
                           }}));
    out.push_back(checked({"balance", kIn, kOut, {{"position", Kind::real, "0", {}, -1, 1}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               auto a = audio(in);
                               if (auto e = need_stereo("balance", a)) return std::unexpected{*e};
                               const double position = param_real(p, "position");
                               const double gains[2] = {std::sqrt(1 - position), std::sqrt(1 + position)};
                               for (std::size_t ch = 0; ch < 2; ++ch)
                                   for (auto& v : a.channels[ch]) v = gains[ch] * v;
                               return one(std::move(a));
                           }}));
    out.push_back(checked({"sample_hold", kIn, kOut, {{"hold", Kind::samples, "4"}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto hold = param_samples(p, "hold", a.sample_rate);
                               if (!hold) return std::unexpected{hold.error()};
                               if (*hold == 0) return fail(tr("pipeline.error.zero_samples"), fmt::arg("block", "sample_hold"), fmt::arg("param", "hold"));
                               return each_channel(a, [h = *hold](const Samples& c) {
                                   Samples y(c.size());
                                   for (std::size_t k = 0; k < c.size(); ++k) y[k] = c[k - k % h];
                                   return y;
                               });
                           }}));
    out.push_back(checked({"dc_block", kIn, kOut, {{"r", Kind::real, "0.995", {}, 0, 0.999999}}, [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const double r = param_real(p, "r");
                               return each_channel(audio(in), [r](const Samples& c) {
                                   Samples y(c.size());
                                   double state = 0;
                                   for (std::size_t k = 0; k < c.size(); ++k) {
                                       y[k] = c[k] + state;
                                       state = -c[k] + r * y[k];
                                   }
                                   return y;
                               });
                           }}));
}

}  // namespace signals::pipeline
