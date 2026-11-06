#include "tui/screens/explain_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

#include <fmt/format.h>
#include <toml++/toml.hpp>

#include "core/design/families.hpp"
#include "core/matrix.hpp"
#include "core/wavedec.hpp"
#include "i18n/i18n.hpp"
#include "tui/widgets/format.hpp"

extern const char kSignalsExplainHaar[];

namespace signals::tui {
namespace {

constexpr double kTol = 1e-12;

std::vector<double> full_conv(std::span<const double> x, const std::vector<double>& h) {
    if (x.empty() || h.empty()) return {};
    std::vector<double> y(x.size() + h.size() - 1, 0.0);
    for (std::size_t i = 0; i < x.size(); ++i)
        for (std::size_t k = 0; k < h.size(); ++k) y[i + k] += x[i] * h[k];
    return y;
}

double energy(std::span<const double> v) {
    double s = 0;
    for (double x : v) s += x * x;
    return s;
}

double max_magnitude(const core::Laurent& p) {
    const auto r = core::frequency_response(p, 1024);
    return r.magnitude.empty() ? 0.0 : *std::max_element(r.magnitude.begin(), r.magnitude.end());
}

double to_db(double m) { return m > 0 ? 20 * std::log10(m) : -std::numeric_limits<double>::infinity(); }

std::string vector_text(const std::vector<double>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? "; " : "") + format_cell(v[i], false);
    return out + "]";
}

bool dyadic(double v, long long& num, int& shift) {
    for (shift = 0; shift <= 20; ++shift) {
        const double scaled = std::ldexp(v, shift);
        if (std::abs(scaled) < 1e15 && scaled == std::round(scaled)) {
            num = static_cast<long long>(scaled);
            return true;
        }
    }
    return false;
}

std::string with_minus(std::string s) {
    if (!s.empty() && s[0] == '-') return "−" + s.substr(1);
    return s;
}

}  // namespace

std::string format_cell(double v, bool fractions) {
    if (std::abs(v) < 1e-12) return "0";
    long long num = 0;
    int shift = 0;
    if (fractions && dyadic(v, num, shift)) {
        while (shift > 0 && num % 2 == 0) num /= 2, --shift;
        if (shift == 0) return with_minus(std::to_string(num));
        return with_minus(std::to_string(num) + "/" + std::to_string(1LL << shift));
    }
    auto s = fmt::format("{:.3f}", v);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    if (s == "-0" || s == "0") return "0";
    std::replace(s.begin(), s.end(), '.', ',');
    return with_minus(s);
}

core::Result<Walkthrough> walkthrough(std::span<const double> x, const ExplainOptions& options) {
    if (x.empty()) return core::make_error(core::Errc::invalid_argument, "walkthrough: empty signal");
    Walkthrough w;
    w.options = options;
    core::Result<core::FilterBank> bank = core::make_error(core::Errc::invalid_argument, "walkthrough: no bank");
    switch (options.bank) {
    case ExplainBank::haar: bank = core::wavelet("haar"), w.bank_name = "Хаар"; break;
    case ExplainBank::db2: bank = core::wavelet("db2"), w.bank_name = "db2"; break;
    case ExplainBank::legall53: bank = core::wavelet("bior2.2"), w.bank_name = "5/3"; break;
    case ExplainBank::user:
        if (options.user) bank = *options.user, w.bank_name = options.user->name;
        break;
    }
    if (!bank) return std::unexpected{bank.error()};
    w.bank = *bank;
    if (options.summing) {
        const double s = 1 / std::sqrt(2.0);
        w.bank.h0 = core::scale(w.bank.h0, s);
        w.bank.h1 = core::scale(w.bank.h1, s);
        w.bank.f0 = core::scale(w.bank.f0, 1 / s);
        w.bank.f1 = core::scale(w.bank.f1, 1 / s);
    }
    if (options.perturb_f1) w.bank.f1 = core::scale(w.bank.f1, 1.01);
    const auto& h0 = w.bank.h0.coeffs;
    const auto& h1 = w.bank.h1.coeffs;
    const std::size_t L = w.bank.length();
    w.x.assign(x.begin(), x.end());
    w.y0 = full_conv(x, h0);
    w.y1 = full_conv(x, h1);
    for (std::size_t m = options.odd_phase ? 1 : 0; m < w.y0.size(); m += 2) {
        w.kept.push_back(m);
        w.a.push_back(w.y0[m]);
        w.d.push_back(w.y1[m]);
    }
    w.u0.assign(2 * w.a.size(), 0.0);
    w.u1.assign(2 * w.d.size(), 0.0);
    for (std::size_t k = 0; k < w.a.size(); ++k) {
        w.u0[2 * k] = w.a[k];
        w.u1[2 * k] = w.d[k];
    }
    w.w0 = full_conv(w.u0, w.bank.f0.coeffs);
    w.w1 = full_conv(w.u1, w.bank.f1.coeffs);
    w.xhat.resize(w.w0.size());
    for (std::size_t i = 0; i < w.xhat.size(); ++i) w.xhat[i] = w.w0[i] + (options.zero_detail ? 0.0 : w.w1[i]);
    w.window = L - (options.odd_phase ? 2 : 1);
    for (std::size_t n = 0; n < x.size(); ++n) {
        const auto i = w.window + n;
        const double v = i < w.xhat.size() ? w.xhat[i] : 0.0;
        w.max_error = std::max(w.max_error, std::abs(v - x[n]));
    }
    w.energy_x = energy(x);
    w.energy_a = energy(w.a);
    w.energy_d = energy(w.d);
    const auto pr = core::check_pr(w.bank, 1e-9);
    w.delay = pr.delay.value_or(0);
    w.orthogonal = core::check_orthogonality(w.bank, 1e-9).passed();
    const auto max_level = core::dwt_max_level(x.size(), L);
    if (max_level >= 1)
        if (auto c = core::wavedec<double>(x, w.bank, std::min<std::size_t>(3, max_level), {core::Extension::zero}); c) w.tree = std::move(c->bands);
    if (auto m = core::analysis_matrix(w.bank, 8, {core::Extension::periodization}); m) w.a1 = std::move(*m);
    w.h0_response = core::frequency_response(w.bank.h0, 256);
    w.h1_response = core::frequency_response(w.bank.h1, 256);
    w.branch_alias_db = to_db(max_magnitude(core::scale(core::multiply(w.bank.f0, core::alternate(w.bank.h0)), 0.5)));
    w.max_alias_db = options.zero_detail ? w.branch_alias_db : to_db(max_magnitude(core::alias_transfer(w.bank)));
    return w;
}

Invariant check_invariant(std::string_view id, const Walkthrough& w) {
    const double scale = std::max(1.0, w.energy_x);
    double peak = 1;
    for (double v : w.x) peak = std::max(peak, std::abs(v));
    if (id == "energy") return {w.energy_x, 0, true};
    if (id == "conv_length") return {static_cast<double>(w.y0.size()), 0, w.y0.size() == w.x.size() + w.bank.length() - 1};
    if (id == "coefficient_count") return {static_cast<double>(w.a.size() + w.d.size()), 0, w.a.size() == w.kept.size() && w.d.size() == w.kept.size()};
    if (id == "upsampled_length") return {static_cast<double>(w.u0.size()), 0, w.u0.size() == 2 * w.a.size()};
    if (id == "power_complementary") {
        double worst = 0;
        for (std::size_t k = 0; k < w.h0_response.magnitude.size() && k < w.h1_response.magnitude.size(); ++k) {
            const double a = w.h0_response.magnitude[k], b = w.h1_response.magnitude[k];
            const double gain = w.options.summing ? 0.5 : 2.0;
            worst = std::max(worst, std::abs(a * a + b * b - gain));
        }
        return {worst, kTol, worst <= kTol};
    }
    if (id == "parseval") {
        const double v = std::abs(w.energy_x - w.energy_a - w.energy_d) / scale;
        return {v, kTol, v <= kTol};
    }
    if (id == "synthesis_split" || id == "reconstruction") return {w.max_error, kTol * peak, w.max_error <= kTol * peak};
    if (id == "orthogonal_matrix") {
        if (w.a1.size() == 0) return {std::numeric_limits<double>::quiet_NaN(), kTol, false};
        Eigen::MatrixXd product;
        if (w.orthogonal) {
            product = w.a1.transpose() * w.a1;
        } else {
            auto s = core::synthesis_matrix(w.bank, 8, {core::Extension::periodization});
            if (!s) return {std::numeric_limits<double>::quiet_NaN(), kTol, false};
            product = *s * w.a1;
        }
        const double v = (product - Eigen::MatrixXd::Identity(product.rows(), product.cols())).cwiseAbs().maxCoeff();
        return {v, kTol, v <= kTol};
    }
    if (id == "tree_energy") {
        if (w.tree.empty()) return {std::numeric_limits<double>::quiet_NaN(), kTol, false};
        double total = 0;
        for (const auto& b : w.tree) total += energy(b);
        const double v = std::abs(total - w.energy_x) / scale;
        return {v, kTol, v <= kTol};
    }
    if (id == "alias_cancelled") {
        const auto t = core::as_monomial(core::distortion_transfer(w.bank), 1e-9);
        return {w.max_alias_db, -250, t.has_value() && w.max_alias_db <= -250};
    }
    if (id == "zero_detail_error") {
        double err = 0, branch = 0;
        for (std::size_t n = 0; n < w.x.size(); ++n) {
            const auto i = w.window + n;
            const double xh = i < w.w0.size() ? w.w0[i] : 0.0;
            err = std::max(err, std::abs(w.x[n] - xh));
            branch = std::max(branch, i < w.w1.size() ? std::abs(w.w1[i]) : 0.0);
        }
        const double v = std::abs(err - branch);
        return {v, kTol * peak, v <= kTol * peak};
    }
    return {std::numeric_limits<double>::quiet_NaN(), 0, false};
}

core::Result<std::vector<ExplainStep>> load_script(std::string_view text) {
    toml::table table;
    try {
        table = toml::parse(text);
    } catch (const toml::parse_error& e) {
        return core::make_error(core::Errc::invalid_argument, std::string{"explanation script is not valid TOML: "} + std::string{e.description()});
    }
    const auto* steps = table["step"].as_array();
    if (!steps) return core::make_error(core::Errc::invalid_argument, "explanation script has no [[step]]");
    std::vector<ExplainStep> out;
    for (const auto& node : *steps) {
        const auto* t = node.as_table();
        if (!t) return core::make_error(core::Errc::invalid_argument, "explanation step is not a table");
        ExplainStep s;
        auto str = [&](std::string_view key, std::string& into) -> bool {
            const auto* v = t->get(key);
            if (!v) return true;
            const auto* s2 = v->as_string();
            if (!s2) return false;
            into = s2->get();
            return true;
        };
        auto list = [&](std::string_view key, std::vector<std::string>& into) -> bool {
            const auto* v = t->get(key);
            if (!v) return true;
            const auto* a = v->as_array();
            if (!a) return false;
            for (const auto& e : *a) {
                const auto* s2 = e.as_string();
                if (!s2) return false;
                into.push_back(s2->get());
            }
            return true;
        };
        if (const auto* m = t->get("mark_kept")) {
            if (!m->as_boolean()) return core::make_error(core::Errc::invalid_argument, "explanation step mark_kept must be a boolean");
            s.mark_kept = m->as_boolean()->get();
        }
        if (!str("title", s.title) || !str("invariant", s.invariant) || !str("view", s.view) || !list("highlight", s.highlight) ||
            !list("rows", s.rows) || !list("text", s.text) || !list("text_haar", s.text_haar) || s.title.empty())
            return core::make_error(core::Errc::invalid_argument, "explanation step " + std::to_string(out.size() + 1) + " is malformed");
        out.push_back(std::move(s));
    }
    return out;
}

const std::vector<ExplainStep>& haar_script() {
    static const std::vector<ExplainStep> script = [] {
        auto s = load_script(kSignalsExplainHaar);
        return s ? std::move(*s) : std::vector<ExplainStep>{};
    }();
    return script;
}

core::Result<std::string> fill(std::string_view line, const Walkthrough& w) {
    auto num = [](double v) { return format_number(v); };
    auto tree_band = [&](std::size_t i) { return i < w.tree.size() ? vector_text(w.tree[i]) : std::string{"—"}; };
    std::map<std::string, std::string, std::less<>> values{
        {"n", std::to_string(w.x.size())},
        {"L", std::to_string(w.bank.length())},
        {"h0", vector_text(w.bank.h0.coeffs)},
        {"h1", vector_text(w.bank.h1.coeffs)},
        {"f0", vector_text(w.bank.f0.coeffs)},
        {"f1", vector_text(w.bank.f1.coeffs)},
        {"conv_len", std::to_string(w.y0.size())},
        {"a_count", std::to_string(w.a.size())},
        {"d_count", std::to_string(w.d.size())},
        {"u_len", std::to_string(w.u0.size())},
        {"x.energy", num(w.energy_x)},
        {"a.energy", num(w.energy_a)},
        {"d.energy", num(w.energy_d)},
        {"a.share", w.energy_x > 0 ? format_number(100 * w.energy_a / w.energy_x) + " %" : "—"},
        {"error", w.max_error < 1e-300 ? "0" : format_number(w.max_error, 2)},
        {"delay", std::to_string(w.delay)},
        {"parity", tr(w.options.odd_phase ? "tui.explain.parity.odd" : "tui.explain.parity.even")},
        {"phase", tr(w.options.odd_phase ? "tui.explain.phase.odd" : "tui.explain.phase.even")},
        {"shift", tr(w.options.odd_phase ? "tui.explain.shift.none" : "tui.explain.shift.one")},
        {"matrix_identity", w.orthogonal ? "A₁ᵀA₁ = I" : "S·A₁ = I"},
        {"a2", w.tree.size() == 3 ? tree_band(0) : std::string{"—"}},
        {"d2", w.tree.size() >= 3 ? tree_band(w.tree.size() - 2) : std::string{"—"}},
        {"a3", w.tree.size() == 4 ? tree_band(0) : std::string{"—"}},
        {"d3", w.tree.size() == 4 ? tree_band(1) : std::string{"—"}},
        {"alias_db", std::isinf(w.max_alias_db) ? "−∞" : format_fixed(w.max_alias_db, 2)},
    };
    {
        std::string kept;
        for (std::size_t i = 0; i < w.kept.size(); ++i) kept += (i ? ", " : "") + std::to_string(w.kept[i]);
        values["kept"] = kept;
    }
    if (w.tree.size() == 4)
        if (auto up = core::idwt_level<double>(w.tree[0], w.tree[1], w.bank, {core::Extension::zero}); up) {
            up->resize(w.tree[2].size());
            values["a2"] = vector_text(*up);
        }
    if (!w.tree.empty()) {
        std::string parts;
        double total = 0;
        for (std::size_t i = 0; i < w.tree.size(); ++i) {
            const double e = energy(w.tree[i]);
            total += e;
            parts += (i ? " + " : "") + num(e);
        }
        values["tree.energy"] = parts + " = " + num(total);
    } else {
        values["tree.energy"] = "—";
    }
    {
        const auto& r = w.h0_response;
        double peak = 0;
        for (double m : r.magnitude) peak = std::max(peak, m);
        std::string cutoff = "—";
        const double target = peak / std::sqrt(2.0);
        for (std::size_t k = 1; k < r.magnitude.size(); ++k)
            if (r.magnitude[k - 1] >= target && r.magnitude[k] < target) {
                const double t = (r.magnitude[k - 1] - target) / (r.magnitude[k - 1] - r.magnitude[k]);
                cutoff = format_fixed((r.omega[k - 1] + t * (r.omega[k] - r.omega[k - 1])) / std::numbers::pi, 3) + "π";
                break;
            }
        values["cutoff"] = cutoff;
        const auto h1 = core::evaluate(w.bank.h1, std::numbers::pi / 4);
        double peak1 = 0;
        for (double m : w.h1_response.magnitude) peak1 = std::max(peak1, m);
        values["leak"] = peak1 > 0 ? format_fixed(to_db(std::abs(h1) / peak1), 1) + " дБ" : "—";
    }
    std::string out;
    std::size_t pos = 0;
    while (pos < line.size()) {
        const auto open = line.find("{{", pos);
        if (open == std::string_view::npos) {
            out.append(line.substr(pos));
            break;
        }
        const auto close = line.find("}}", open);
        if (close == std::string_view::npos) return core::make_error(core::Errc::invalid_argument, "unclosed placeholder in: " + std::string{line});
        out.append(line.substr(pos, open - pos));
        const auto name = line.substr(open + 2, close - open - 2);
        const auto it = values.find(name);
        if (it == values.end()) return core::make_error(core::Errc::invalid_argument, "unknown placeholder {{" + std::string{name} + "}}");
        out += it->second;
        pos = close + 2;
    }
    return out;
}

}  // namespace signals::tui
