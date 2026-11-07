#include "tui/screens/diagram_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace signals::tui {
namespace {

std::vector<double> conv(std::span<const double> x, std::span<const double> h) {
    if (x.empty() || h.empty()) return {};
    std::vector<double> y(x.size() + h.size() - 1, 0.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        for (std::size_t k = 0; k < h.size(); ++k) y[i + k] += x[i] * h[k];
    return y;
}

std::vector<double> plus(std::vector<double> a, const std::vector<double>& b) {
    if (a.size() < b.size()) a.resize(b.size(), 0.0);
    for (std::size_t i = 0; i < b.size(); ++i) a[i] += b[i];
    return a;
}

std::vector<double> phase(std::span<const double> v, std::size_t first) {
    std::vector<double> out;
    for (std::size_t i = first; i < v.size(); i += 2) out.push_back(v[i]);
    return out;
}

}  // namespace

const Wire* DiagramResult::wire(std::string_view id, bool polyphase_view) const {
    for (const auto& w : polyphase_view ? polyphase : direct)
        if (w.id == id) return &w;
    return nullptr;
}

core::Result<DiagramResult> run_diagram(const core::FilterBank& bank, std::span<const double> x) {
    if (x.empty()) return core::make_error(core::Errc::invalid_argument, "diagram: the input is empty");
    const auto& h0 = bank.h0.coeffs;
    const auto& h1 = bank.h1.coeffs;
    const auto& f0 = bank.f0.coeffs;
    const auto& f1 = bank.f1.coeffs;
    DiagramResult r;
    r.bank = bank.name;
    const std::vector<double> input(x.begin(), x.end());
    const auto y0 = conv(x, h0), y1 = conv(x, h1);
    const auto a = phase(y0, 1), d = phase(y1, 1);
    std::vector<double> u0(2 * a.size(), 0.0), u1(2 * d.size(), 0.0);
    for (std::size_t k = 0; k < a.size(); ++k) u0[2 * k] = a[k];
    for (std::size_t k = 0; k < d.size(); ++k) u1[2 * k] = d[k];
    const auto v0 = conv(u0, f0), v1 = conv(u1, f1);
    const auto xh = plus(v0, v1);
    r.direct = {{"x", input}, {"y0", y0}, {"y1", y1}, {"a", a}, {"d", d}, {"u0", u0}, {"u1", u1}, {"v0", v0}, {"v1", v1}, {"xh", xh}};

    const auto xe = phase(x, 0), xo = phase(x, 1);
    const auto h0e = phase(h0, 0), h0o = phase(h0, 1), h1e = phase(h1, 0), h1o = phase(h1, 1);
    const auto pa = plus(conv(xe, h0o), conv(xo, h0e));
    const auto pd = plus(conv(xe, h1o), conv(xo, h1e));
    const auto f0e = phase(f0, 0), f0o = phase(f0, 1), f1e = phase(f1, 0), f1o = phase(f1, 1);
    const auto we = plus(conv(pa, f0e), conv(pd, f1e));
    const auto wo = plus(conv(pa, f0o), conv(pd, f1o));
    std::vector<double> pxh(std::max(2 * we.size(), 2 * wo.size()), 0.0);
    for (std::size_t n = 0; n < we.size(); ++n) pxh[2 * n] = we[n];
    for (std::size_t n = 0; n < wo.size(); ++n) pxh[2 * n + 1] = wo[n];
    r.polyphase = {{"x", input}, {"xe", xe}, {"xo", xo}, {"a", pa}, {"d", pd}, {"we", we}, {"wo", wo}, {"xh", pxh}};

    double best = std::numeric_limits<double>::infinity();
    const auto span = static_cast<std::ptrdiff_t>(h0.size() + h1.size() + f0.size() + f1.size());
    for (std::ptrdiff_t s = 0; s <= span; ++s) {
        double worst = 0;
        for (std::size_t n = 0; n < input.size(); ++n) {
            const auto i = static_cast<std::size_t>(s) + n;
            worst = std::max(worst, std::abs((i < xh.size() ? xh[i] : 0.0) - input[n]));
        }
        if (worst < best) best = worst, r.delay = s;
    }
    r.error = best;
    r.direct_mults = static_cast<double>(h0.size() + h1.size() + f0.size() + f1.size());
    r.polyphase_mults = r.direct_mults / 2;
    return r;
}

}  // namespace signals::tui
