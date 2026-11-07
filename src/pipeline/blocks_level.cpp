#include <cmath>
#include <numbers>

#include "core/design/fir.hpp"
#include "core/signal_ops.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

Samples tukey(std::size_t n, double alpha) {
    Samples w(n, 1.0);
    if (n < 2 || alpha <= 0) return w;
    if (alpha >= 1) return core::symmetric_window(core::FirWindow::hann, n, 0);
    const double last = static_cast<double>(n - 1);
    const auto width = static_cast<std::size_t>(std::floor(alpha * last / 2.0));
    for (std::size_t k = 0; k <= width; ++k)
        w[k] = 0.5 * (1 + std::cos(std::numbers::pi * (-1 + 2.0 * static_cast<double>(k) / alpha / last)));
    for (std::size_t k = n - width - 1; k < n; ++k)
        w[k] = 0.5 * (1 + std::cos(std::numbers::pi * (-2.0 / alpha + 1 + 2.0 * static_cast<double>(k) / alpha / last)));
    return w;
}

double fade_gain(std::size_t i, std::size_t length, bool cosine) {
    const double t = static_cast<double>(i) / static_cast<double>(length);
    return cosine ? 0.5 - 0.5 * std::cos(std::numbers::pi * t) : t;
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

}  // namespace

void register_level_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"pad", kIn, kOut, {{"before", Kind::samples, "0"}, {"after", Kind::samples, "0"}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto before = param_samples(p, "before", a.sample_rate), after = param_samples(p, "after", a.sample_rate);
                               if (!before) return std::unexpected{before.error()};
                               if (!after) return std::unexpected{after.error()};
                               return each_channel(a, [&](const Samples& c) -> core::Result<Samples> {
                                   if (auto n = checked_length(c.size() + *before + *after, 1); !n) return std::unexpected{n.error()};
                                   Samples y(*before, 0.0);
                                   y.insert(y.end(), c.begin(), c.end());
                                   y.resize(y.size() + *after, 0.0);
                                   return y;
                               });
                           }}));
    out.push_back(checked({"concat", kPair, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) -> Out {
                               return pairwise("concat", audio(in, 0), audio(in, 1), [](const Samples& x, const Samples& y) -> core::Result<Samples> {
                                   if (auto n = checked_length(x.size() + y.size(), 1); !n) return std::unexpected{n.error()};
                                   Samples z = x;
                                   z.insert(z.end(), y.begin(), y.end());
                                   return z;
                               });
                           }}));
    out.push_back(checked({"multiply",
                           {{"a", PortType::signal}, {"b", PortType::signal, true}},
                           kOut,
                           {{"factor", Kind::real, "1", {}, -1e6, 1e6}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const double factor = param_real(p, "factor");
                               const auto& b = audio(in, 1);
                               if (b.channels.empty()) return each_sample(in, [factor](double x) { return factor * x; });
                               return pairwise("multiply", audio(in, 0), b, [factor](const Samples& x, const Samples& y) -> core::Result<Samples> {
                                   Samples z(std::max(x.size(), y.size()), 0.0);
                                   for (std::size_t i = 0; i < std::min(x.size(), y.size()); ++i) z[i] = factor * x[i] * y[i];
                                   return z;
                               });
                           }}));
    out.push_back(checked({"window",
                           kIn,
                           kOut,
                           {{"kind", Kind::choice, "hann", {"hann", "hamming", "blackman", "kaiser", "tukey"}},
                            {"beta", Kind::real, "8.6", {}, 0, 100},
                            {"alpha", Kind::real, "0.5", {}, 0, 1}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& kind = param_text(p, "kind");
                               return each_channel(audio(in), [&](const Samples& c) {
                                   const auto w = kind == "tukey" ? tukey(c.size(), param_real(p, "alpha"))
                                                                  : core::symmetric_window(*core::parse_fir_window(kind), c.size(), param_real(p, "beta"));
                                   Samples y(c.size());
                                   for (std::size_t i = 0; i < c.size(); ++i) y[i] = c[i] * w[i];
                                   return y;
                               });
                           }}));
    out.push_back(checked({"fade",
                           kIn,
                           kOut,
                           {{"fade_in", Kind::samples, "0.01s"}, {"fade_out", Kind::samples, "0.01s"}, {"curve", Kind::choice, "linear", {"linear", "cosine"}}},
                           [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const auto rise = param_samples(p, "fade_in", a.sample_rate), fall = param_samples(p, "fade_out", a.sample_rate);
                               if (!rise) return std::unexpected{rise.error()};
                               if (!fall) return std::unexpected{fall.error()};
                               const bool cosine = param_text(p, "curve") == "cosine";
                               return each_channel(a, [&](const Samples& c) {
                                   Samples y = c;
                                   for (std::size_t i = 0; i < std::min(*rise, y.size()); ++i) y[i] *= fade_gain(i, *rise, cosine);
                                   for (std::size_t i = 0; i < std::min(*fall, y.size()); ++i) y[y.size() - 1 - i] *= fade_gain(i, *fall, cosine);
                                   return y;
                               });
                           }}));
    out.push_back(checked({"invert", kIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return each_channel(audio(in), [](const Samples& c) { return core::invert(c); });
                           }}));
    out.push_back(checked({"abs", kIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return each_sample(in, [](double x) { return std::abs(x); });
                           }}));
    out.push_back(checked({"square", kIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return each_sample(in, [](double x) { return x * x; });
                           }}));
    out.push_back(checked({"diff", kIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return each_channel(audio(in), [](const Samples& c) {
                                   Samples y(c.size());
                                   for (std::size_t i = 0; i < c.size(); ++i) y[i] = c[i] - (i ? c[i - 1] : 0.0);
                                   return y;
                               });
                           }}));
    out.push_back(checked({"cumsum", kIn, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return each_channel(audio(in), [](const Samples& c) {
                                   Samples y(c.size());
                                   double sum = 0;
                                   for (std::size_t i = 0; i < c.size(); ++i) y[i] = sum += c[i];
                                   return y;
                               });
                           }}));
    out.push_back(checked({"softclip", kIn, kOut, {{"drive", Kind::real, "1", {}, 0.01, 1000}}, [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const double drive = param_real(p, "drive");
                               return each_sample(in, [drive](double x) { return std::tanh(drive * x); });
                           }}));
    out.push_back(checked({"dc_offset", kIn, kOut, {{"value", Kind::real, "0", {}, -1e6, 1e6}}, [](const RunContext&, std::span<const Value> in, const Params& p) {
                               const double value = param_real(p, "value");
                               return each_sample(in, [value](double x) { return x + value; });
                           }}));
    out.push_back(checked({"trim_silence", kIn, kOut, {{"threshold_db", Kind::real, "-60", {}, -200, 0}}, [](const RunContext&, std::span<const Value> in, const Params& p) -> Out {
                               const auto& a = audio(in);
                               const double threshold = std::pow(10.0, param_real(p, "threshold_db") / 20);
                               const auto loud = [&](std::size_t i) {
                                   return std::ranges::any_of(a.channels, [&](const Samples& c) { return i < c.size() && std::abs(c[i]) >= threshold; });
                               };
                               std::size_t begin = 0, end = a.frames();
                               while (begin < end && !loud(begin)) ++begin;
                               while (end > begin && !loud(end - 1)) --end;
                               return each_channel(a, [&](const Samples& c) {
                                   return Samples(c.begin() + static_cast<std::ptrdiff_t>(std::min(begin, c.size())), c.begin() + static_cast<std::ptrdiff_t>(std::min(end, c.size())));
                               });
                           }}));
}

}  // namespace signals::pipeline
