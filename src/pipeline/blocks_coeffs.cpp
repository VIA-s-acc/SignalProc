#include <algorithm>
#include <cmath>
#include <functional>

#include "core/design/families.hpp"
#include "core/packets.hpp"
#include "pipeline/blocks_common.hpp"

namespace signals::pipeline {
namespace {

using namespace blocks;

const std::vector<std::string> kExtensions{"symmetric", "periodization", "zero", "constant", "smooth", "periodic", "reflect", "antisymmetric", "antireflect"};

Out polyphase(std::span<const Value> in) {
    const auto& a = audio(in);
    core::AudioBuffer even, odd;
    even.sample_rate = odd.sample_rate = a.sample_rate / 2;
    for (const auto& c : a.channels) {
        Samples e((c.size() + 1) / 2), o(c.size() / 2);
        for (std::size_t i = 0; i < c.size(); ++i) (i % 2 ? o[i / 2] : e[i / 2]) = c[i];
        even.channels.push_back(std::move(e));
        odd.channels.push_back(std::move(o));
    }
    return std::vector<Value>{std::move(even), std::move(odd)};
}

Out ipolyphase(std::span<const Value> in) {
    const auto& even = audio(in, 0);
    const auto& odd = audio(in, 1);
    if (auto e = same_rate("ipolyphase", even, odd)) return std::unexpected{*e};
    if (even.channels.size() != odd.channels.size())
        return fail(tr("pipeline.error.ipolyphase_channels"), fmt::arg("even", even.channels.size()), fmt::arg("odd", odd.channels.size()));
    core::AudioBuffer out;
    out.sample_rate = even.sample_rate * 2;
    for (std::size_t ch = 0; ch < even.channels.size(); ++ch) {
        const auto& e = even.channels[ch];
        const auto& o = odd.channels[ch];
        if (e.size() != o.size() && e.size() != o.size() + 1) return fail(tr("pipeline.error.ipolyphase_lengths"), fmt::arg("even", e.size()), fmt::arg("odd", o.size()));
        Samples y(e.size() + o.size());
        for (std::size_t i = 0; i < e.size(); ++i) y[2 * i] = e[i];
        for (std::size_t i = 0; i < o.size(); ++i) y[2 * i + 1] = o[i];
        out.channels.push_back(std::move(y));
    }
    return one(std::move(out));
}

std::vector<core::PacketNode> pick_basis(const std::string& kind, std::span<const core::PacketTree> trees, std::size_t levels) {
    if (kind == "full") return core::level_basis(levels);
    if (kind == "dwt") return core::dwt_basis(levels);
    return core::best_basis(trees, core::PacketCost::shannon);
}

Out wpt(const RunContext& ctx, std::span<const Value> in, const Params& p) {
    const auto& name = param_text(p, "wavelet");
    auto bank = core::wavelet(name);
    if (!bank) return std::unexpected{bank.error()};
    const auto& a = audio(in);
    const auto levels = static_cast<std::size_t>(param_int(p, "levels"));
    Coeffs c;
    c.wavelet = name;
    c.mode = *core::parse_extension(param_text(p, "ext"));
    c.rate = a.sample_rate;
    c.transform = "wpt";
    std::vector<core::PacketTree> trees;
    for (const auto& ch : a.channels) {
        if (ch.size() < (std::size_t{1} << levels))
            return fail(tr("pipeline.error.wpt_levels"), fmt::arg("levels", tr_count("unit.level", static_cast<long long>(levels))),
                        fmt::arg("n", tr_count("unit.sample", static_cast<long long>(ch.size()))));
        auto tree = core::wp_decompose(ch, *bank, levels, c.mode, ctx.progress);
        if (!tree) return std::unexpected{tree.error()};
        trees.push_back(std::move(*tree));
    }
    c.basis = pick_basis(param_text(p, "basis"), trees, levels);
    for (std::size_t i = 0; i < trees.size(); ++i) {
        core::BasicCoeffs<double> bands;
        bands.original_length = a.channels[i].size();
        for (const auto& node : c.basis) bands.bands.push_back(std::move(trees[i].nodes[node.level][node.index]));
        c.channels.push_back(std::move(bands));
        c.lengths.push_back(a.channels[i].size());
    }
    return one(std::move(c));
}

Out iwpt(const RunContext& ctx, std::span<const Value> in) {
    const auto& c = coeffs(in);
    if (c.transform != "wpt") return fail(tr("pipeline.error.wrong_inverse"), fmt::arg("transform", c.transform), fmt::arg("inverse", inverse_of(c.transform)));
    auto bank = core::wavelet(c.wavelet);
    if (!bank) return std::unexpected{bank.error()};
    std::size_t depth = 0;
    for (const auto& node : c.basis) depth = std::max(depth, node.level);
    if (!core::admissible(c.basis, depth)) return fail(tr("pipeline.error.iwpt_basis"));
    core::AudioBuffer out;
    out.sample_rate = c.rate;
    for (std::size_t i = 0; i < c.channels.size(); ++i) {
        const auto& bands = c.channels[i].bands;
        const auto n = i < c.lengths.size() ? c.lengths[i] : c.channels[i].original_length;
        if (bands.size() != c.basis.size() || n == 0) return fail(tr("pipeline.error.iwpt_basis"));
        auto tree = core::wp_decompose(Samples(n, 0.0), *bank, depth, c.mode, ctx.progress);
        if (!tree) return std::unexpected{tree.error()};
        for (std::size_t k = 0; k < bands.size(); ++k) {
            auto& node = tree->nodes[c.basis[k].level][c.basis[k].index];
            if (node.size() != bands[k].size()) return fail(tr("pipeline.error.iwpt_basis"));
            node = bands[k];
        }
        auto y = core::wp_reconstruct(*tree, c.basis, *bank, c.mode);
        if (!y) return std::unexpected{y.error()};
        y->resize(n);
        out.channels.push_back(std::move(*y));
    }
    return one(std::move(out));
}

Out keep_largest(const RunContext& ctx, std::span<const Value> in, const Params& p) {
    if (!finite(in[0])) return fail(tr("pipeline.error.input_not_finite"), fmt::arg("block", "keep_largest"));
    const bool percent = param_text(p, "mode") == "percent";
    const double amount = param_real(p, "amount");
    if (percent && amount > 100) return fail(tr("pipeline.error.keep_percent"), fmt::arg("value", amount));
    const std::size_t first = param_text(p, "keep_approx") == "yes" ? 1 : 0;
    Coeffs c = take<Coeffs>(ctx, in, 0);
    for (auto& ch : c.channels) {
        std::vector<double*> pool;
        for (std::size_t b = first; b < ch.bands.size(); ++b)
            for (double& v : ch.bands[b]) pool.push_back(&v);
        const auto total = static_cast<double>(pool.size());
        const auto keep = std::min(pool.size(), static_cast<std::size_t>(std::llround(percent ? amount / 100 * total : amount)));
        std::ranges::stable_sort(pool, std::greater<>{}, [](const double* v) { return std::abs(*v); });
        for (std::size_t k = keep; k < pool.size(); ++k) *pool[k] = 0.0;
    }
    return one(std::move(c));
}

Out coeff_quantize(const RunContext& ctx, std::span<const Value> in, const Params& p) {
    const bool by_bits = param_text(p, "mode") == "bits";
    const double step = param_real(p, "step");
    const double levels = std::ldexp(1.0, static_cast<int>(param_int(p, "bits")) - 1) - 1;
    Coeffs c = take<Coeffs>(ctx, in, 0);
    for (auto& ch : c.channels)
        for (std::size_t b = 1; b < ch.bands.size(); ++b) {
            double delta = step;
            if (by_bits) {
                double peak = 0;
                for (const double v : ch.bands[b]) peak = std::max(peak, std::abs(v));
                delta = peak / levels;
            }
            if (!(delta > 0)) continue;
            for (double& v : ch.bands[b]) v = std::copysign(std::floor(std::abs(v) / delta + 0.5), v) * delta;
        }
    return one(std::move(c));
}

}  // namespace

void register_coeff_blocks(std::vector<BlockType>& out) {
    out.push_back(checked({"polyphase", kIn, {{"even", PortType::signal}, {"odd", PortType::signal}}, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return polyphase(in);
                           }}));
    out.push_back(checked({"ipolyphase", {{"even", PortType::signal}, {"odd", PortType::signal}}, kOut, {}, [](const RunContext&, std::span<const Value> in, const Params&) {
                               return ipolyphase(in);
                           }}));
    out.push_back(checked({"wpt",
                           kIn,
                           kCoeffsOut,
                           {{"wavelet", Kind::text, "db2"},
                            {"levels", Kind::integer, "3", {}, 1, 16},
                            {"basis", Kind::choice, "best", {"best", "full", "dwt"}},
                            {"ext", Kind::choice, "symmetric", kExtensions}},
                           wpt}));
    out.push_back(checked({"iwpt", kCoeffsIn, kOut, {}, [](const RunContext& ctx, std::span<const Value> in, const Params&) { return iwpt(ctx, in); }}));
    out.push_back(checked({"keep_largest",
                           kCoeffsIn,
                           kCoeffsOut,
                           {{"mode", Kind::choice, "percent", {"percent", "count"}},
                            {"amount", Kind::real, "10", {}, 0, 1e9},
                            {"keep_approx", Kind::choice, "yes", {"yes", "no"}}},
                           keep_largest}));
    out.push_back(checked({"coeff_quantize",
                           kCoeffsIn,
                           kCoeffsOut,
                           {{"mode", Kind::choice, "bits", {"bits", "step"}}, {"bits", Kind::integer, "8", {}, 2, 32}, {"step", Kind::real, "0.01", {}, 1e-12, 1e6}},
                           coeff_quantize}));
}

}  // namespace signals::pipeline
