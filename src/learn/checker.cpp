#include "learn/checker.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fmt/format.h>
#include <mutex>
#include <thread>

#include "core/expr.hpp"
#include "i18n/i18n.hpp"
#include "learn/answer.hpp"
#include "learn/structure.hpp"
#include "learn/variant.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/graph.hpp"

namespace signals::learn {

namespace {

using Clock = std::chrono::steady_clock;
using Measures = std::map<std::string, MeasureValue, std::less<>>;

class Watchdog {
public:
    Watchdog(const core::CancelToken& outer, double limit_seconds) : start_{Clock::now()}, limit_{limit_seconds} {
        thread_ = std::thread{[this, &outer] {
            std::unique_lock lock{mutex_};
            while (!stop_) {
                if (outer.cancelled()) inner_.cancel();
                if (elapsed() > limit_) {
                    expired_ = true;
                    inner_.cancel();
                }
                wake_.wait_for(lock, std::chrono::milliseconds{5});
            }
        }};
    }
    ~Watchdog() {
        {
            std::lock_guard lock{mutex_};
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    const core::CancelToken& token() const { return inner_; }
    double elapsed() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }
    bool expired() const { return expired_.load() || elapsed() > limit_; }

private:
    Clock::time_point start_;
    double limit_;
    core::CancelToken inner_;
    std::atomic<bool> expired_{false};
    bool stop_ = false;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
};

std::unexpected<core::Error> task_error(std::string message) { return core::make_error(core::Errc::invalid_argument, std::move(message)); }

std::string say(std::string_view key) { return tr(key); }

template <class... Args>
std::string say(std::string_view key, Args&&... args) {
    return fmt::format(fmt::runtime(tr(key)), std::forward<Args>(args)...);
}

core::Result<pipeline::PipelineDoc> parse(std::string_view text) {
    auto doc = pipeline::parse_sig(text);
    if (!doc) return task_error(say("learn.check.graph_error", fmt::arg("line", doc.error().line), fmt::arg("message", doc.error().message)));
    return std::move(*doc);
}

core::Result<pipeline::RunResult> run(const pipeline::PipelineDoc& doc, const std::filesystem::path& base, const core::CancelToken& cancel) {
    auto graph = pipeline::build_graph(doc, base);
    if (!graph) return task_error(say("learn.check.graph_error", fmt::arg("line", graph.error().line), fmt::arg("message", graph.error().message)));
    pipeline::ResultCache cache{0};
    return pipeline::run_graph(*graph, cache, {}, cancel);
}

std::string hidden_values(const Task& t, int variant, int k, std::string_view values) {
    Rng rng{seed_of(t.id, variant, k)};
    std::string out;
    std::size_t start = 0;
    while (start <= values.size()) {
        auto end = values.find(';', start);
        if (end == std::string_view::npos) end = values.size();
        const auto channel = values.substr(start, end - start);
        std::size_t count = 0;
        bool in_token = false;
        for (char c : channel) {
            const bool space = c == ' ' || c == '\t' || c == ',';
            if (!space && !in_token) ++count;
            in_token = !space;
        }
        if (!out.empty()) out += "; ";
        for (std::size_t i = 0; i < count; ++i) out += (i ? " " : "") + std::to_string(rng.range(-9, 9));
        start = end + 1;
    }
    return out;
}

const pipeline::NodeDecl* find_node(const pipeline::PipelineDoc& doc, std::string_view id) {
    for (const auto& n : doc.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

core::Result<Measures> measure_all(const Task& t, const pipeline::PipelineDoc& doc, const pipeline::RunResult& run,
                                   const pipeline::RunResult* reference, const ParamValues& params) {
    Measures out;
    for (const auto& raw : t.measures) {
        auto m = resolved_measure(raw, params);
        if (!m) return std::unexpected{m.error()};
        auto v = compute_measure(*m, doc, run, reference);
        if (!v) return std::unexpected{v.error()};
        out[m->name] = std::move(*v);
    }
    return out;
}

core::NameLookup names_of(const Measures& measures, const ParamValues& params) {
    return [&](std::string_view name) -> std::optional<double> {
        if (const auto it = measures.find(name); it != measures.end() && !it->second.values.empty()) return it->second.scalar();
        if (const auto it = params.find(name); it != params.end())
            if (auto v = core::eval_number(it->second)) return *v;
        return std::nullopt;
    };
}

core::Result<GoalResult> evaluate(const Goal& g, const pipeline::PipelineDoc& doc, const Measures& measures, const ParamValues& params) {
    GoalResult r;
    if (!g.when.empty()) {
        auto e = core::parse_expr(g.when);
        if (!e) return task_error(describe(e.error()));
        auto v = core::eval(*e, names_of(measures, params));
        if (!v) return task_error(describe(v.error()));
        r.passed = *v != 0;
        for (const auto& name : core::expr_names(*e))
            if (const auto it = measures.find(name); it != measures.end()) {
                r.value = it->second.scalar();
                r.detail = fmt::format("{} = {:.6g}", name, it->second.scalar());
                break;
            }
        return r;
    }
    if (g.through) {
        r.passed = all_paths_through(doc, *g.through);
        if (!r.passed) {
            r.detail = path_exists(doc, g.through->from, g.through->to)
                           ? say("learn.check.bypass_path", fmt::arg("from", g.through->from), fmt::arg("to", g.through->to), fmt::arg("block", g.through->block))
                           : say("learn.check.no_path", fmt::arg("from", g.through->from), fmt::arg("to", g.through->to));
        }
        return r;
    }
    if (!g.forbid.empty()) {
        r.passed = true;
        for (const auto& b : g.forbid)
            if (count_blocks(doc, b) > 0) {
                r.passed = false;
                r.detail = say("learn.check.forbidden", fmt::arg("block", b));
                break;
            }
        return r;
    }
    if (!g.require.empty()) {
        r.passed = true;
        for (const auto& [b, need] : g.require) {
            const auto got = count_blocks(doc, b);
            if (static_cast<int>(got) < need) {
                r.passed = false;
                r.value = static_cast<double>(got);
                r.detail = say("learn.check.required", fmt::arg("block", b), fmt::arg("need", need), fmt::arg("got", got));
                break;
            }
        }
        return r;
    }
    r.passed = std::ranges::any_of(doc.probes, [&](const auto& p) { return p.name == g.probe; });
    if (!r.passed) r.detail = say("learn.check.no_probe", fmt::arg("name", g.probe));
    return r;
}

CheckResult failed(const Task& t, std::string message, double seconds) {
    return CheckResult{false, std::vector<GoalResult>(t.goals.size()), 0, std::move(message), seconds};
}

core::Result<CheckResult> check_answer(const Task& t, int variant, const std::string& text) {
    auto expected = expected_answer(t, variant);
    if (!expected) return std::unexpected{expected.error()};
    auto given = parse_answer(text, *t.answer);
    if (!given) return CheckResult{false, {GoalResult{false, std::nullopt, given.error().message}}, 0, given.error().message, 0};
    const auto cmp = compare_answer(*given, *expected, *t.answer);
    return CheckResult{cmp.passed, {GoalResult{cmp.passed, given->size() == 1 ? std::optional<double>{given->front()} : std::nullopt, cmp.detail}}, 1,
                       std::nullopt, 0};
}

core::Result<Measures> solution_measures(const Task& t, int variant, const core::CancelToken& cancel) {
    auto doc = parse(solution_graph(t, variant));
    if (!doc) return std::unexpected{doc.error()};
    add_measure_probes(*doc, t.measures);
    add_measure_probes(*doc, t.measures, true);
    auto result = run(*doc, ".", cancel);
    if (!result) return std::unexpected{result.error()};
    return measure_all(t, *doc, *result, &*result, draw_params(t.params, t.id, variant));
}

}  // namespace

core::Result<Measure> resolved_measure(const Measure& m, const ParamValues& params) {
    Measure out = m;
    out.delay = substitute(m.delay, params);
    if (!m.freq_text.empty()) {
        auto v = core::eval_number(substitute(m.freq_text, params));
        if (!v) return task_error(say("learn.error.bad_freq", fmt::arg("name", m.name), fmt::arg("why", describe(v.error()))));
        out.freq = *v;
    }
    return out;
}

std::string start_graph(const Task& t, int variant) { return substitute(t.start_sig, draw_params(t.params, t.id, variant)); }

std::string solution_graph(const Task& t, int variant) { return substitute(t.solution_sig, draw_params(t.params, t.id, variant)); }

core::Result<std::map<std::string, MeasureValue, std::less<>>> reference_measures(const Task& t, int variant) {
    core::CancelToken cancel;
    return solution_measures(t, variant, cancel);
}

core::Result<std::vector<double>> expected_answer(const Task& t, int variant) {
    if (!t.answer) return task_error(say("learn.error.no_answer"));
    if (std::ranges::any_of(t.measures, [&](const auto& m) { return m.name == t.answer->expected; })) {
        auto measures = reference_measures(t, variant);
        if (!measures) return std::unexpected{measures.error()};
        return measures->at(t.answer->expected).values;
    }
    return parse_answer(substitute(t.answer->expected, draw_params(t.params, t.id, variant)), *t.answer);
}

core::Result<CheckResult> check_task(const Task& t, int variant, const Submission& s, const core::CancelToken& cancel, double limit_seconds) {
    if (cancel.cancelled()) return core::make_error(core::Errc::cancelled, say("pipeline.run.cancelled"));
    const auto t0 = Clock::now();
    const auto seconds = [&] { return std::chrono::duration<double>(Clock::now() - t0).count(); };
    if (t.kind == TaskKind::answer) {
        auto r = check_answer(t, variant, s.answer.value_or(""));
        if (r) r->seconds = seconds();
        return r;
    }
    const auto params = draw_params(t.params, t.id, variant);
    auto start = parse(start_graph(t, variant));
    if (!start) return std::unexpected{start.error()};
    auto solution = parse(solution_graph(t, variant));
    if (!solution) return std::unexpected{solution.error()};
    auto work = parse(s.sig.value_or(""));
    if (!work) return failed(t, work.error().message, seconds());
    if (const auto changes = locked_changes(*start, *work, t.locked, t.input ? t.input->node : ""); !changes.empty()) {
        std::string message;
        for (const auto& c : changes) message += (message.empty() ? "" : "; ") + c;
        return failed(t, message, seconds());
    }
    std::string input_values;
    if (t.input) {
        const auto* in = find_node(*work, t.input->node);
        if (!in || in->type != "samples") return failed(t, say("learn.check.input_missing", fmt::arg("node", t.input->node)), seconds());
        const auto* original = find_node(*start, t.input->node);
        if (original)
            if (const auto it = original->params.find("values"); it != original->params.end()) input_values = it->second;
    }
    const bool need_reference = std::ranges::any_of(t.measures, [](const auto& m) { return !m.ref.empty(); });
    Watchdog watchdog{cancel, limit_seconds};
    const auto timeout = [&] { return failed(t, say("learn.check.timeout", fmt::arg("limit", fmt::format("{:g}", limit_seconds))), seconds()); };
    CheckResult out{true, std::vector<GoalResult>(t.goals.size()), 0, std::nullopt, 0};
    std::vector<bool> settled(t.goals.size(), false);
    const int hidden = t.input ? std::max(0, t.input->hidden) : 0;
    for (int k = 0; k <= hidden; ++k) {
        auto w = *work;
        auto ref = *solution;
        if (k > 0) {
            const auto set = t.input->node + ".values=\"" + hidden_values(t, variant, k, input_values) + "\"";
            if (auto r = pipeline::apply_set(w, set); !r) return failed(t, r.error().message, seconds());
            if (auto r = pipeline::apply_set(ref, set); !r) return std::unexpected{r.error()};
        }
        add_measure_probes(w, t.measures);
        add_measure_probes(ref, t.measures, true);
        auto result = run(w, s.base_dir, watchdog.token());
        if (!result) {
            if (result.error().code == core::Errc::cancelled) {
                if (watchdog.expired()) return timeout();
                return std::unexpected{result.error()};
            }
            return failed(t, result.error().message, seconds());
        }
        std::optional<pipeline::RunResult> reference;
        if (need_reference) {
            auto r = run(ref, ".", watchdog.token());
            if (!r) {
                if (r.error().code == core::Errc::cancelled) {
                    if (watchdog.expired()) return timeout();
                    return std::unexpected{r.error()};
                }
                return std::unexpected{r.error()};
            }
            reference = std::move(*r);
        }
        auto measures = measure_all(t, w, *result, reference ? &*reference : nullptr, params);
        if (!measures) return failed(t, measures.error().message, seconds());
        for (std::size_t i = 0; i < t.goals.size(); ++i) {
            auto g = evaluate(t.goals[i], w, *measures, params);
            if (!g) return std::unexpected{g.error()};
            if (settled[i]) continue;
            if (!g->passed) {
                if (k > 0) g->detail = say("learn.check.on_hidden", fmt::arg("k", k), fmt::arg("detail", g->detail));
                settled[i] = true;
                out.passed = false;
            }
            if (k == 0 || !g->passed) out.goals[i] = std::move(*g);
        }
        out.inputs = k + 1;
        if (watchdog.expired()) return timeout();
    }
    out.seconds = seconds();
    return out;
}

std::string render_text(std::string_view md, const Task& t, int variant) {
    auto values = draw_params(t.params, t.id, variant);
    auto text = substitute(md, values);
    const auto names = placeholders(text);
    if (std::ranges::find(names, "expected") != names.end() && t.answer)
        if (auto e = expected_answer(t, variant)) values["expected"] = format_answer(*e, *t.answer);
    if (std::ranges::any_of(names, [](const auto& n) { return n.starts_with("m."); }))
        if (auto ms = reference_measures(t, variant))
            for (const auto& [name, v] : *ms) {
                std::string shown;
                for (std::size_t i = 0; i < v.values.size(); ++i) shown += (i ? " " : "") + fmt::format("{:.6g}", v.values[i]);
                values["m." + name] = shown;
            }
    return substitute(text, values);
}

}  // namespace signals::learn
