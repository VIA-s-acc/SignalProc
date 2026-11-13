#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "commands/command.hpp"
#include "core/version.hpp"
#include "i18n/i18n.hpp"
#include "io/config_dir.hpp"
#include "io/temp_file.hpp"
#include "learn/answer.hpp"
#include "learn/checker.hpp"
#include "learn/lint.hpp"
#include "learn/progress.hpp"
#include "learn/report.hpp"
#include "learn/task.hpp"

namespace signals::cli {

namespace {

using namespace signals::learn;

template <class... Args>
std::string say(std::string_view key, Args&&... args) {
    return fmt::format(fmt::runtime(tr(key)), std::forward<Args>(args)...);
}

std::string_view mark(TaskState s) {
    switch (s) {
    case TaskState::solved: return "✓";
    case TaskState::revealed: return "◐";
    case TaskState::open: break;
    }
    return "·";
}

std::string_view state_name(TaskState s) {
    switch (s) {
    case TaskState::solved: return "solved";
    case TaskState::revealed: return "revealed";
    case TaskState::open: break;
    }
    return "open";
}

std::optional<std::string> read_text(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

class LearnCommand final : public Command {
public:
    explicit LearnCommand(CLI::App& app) : root_{app.add_subcommand("learn", tr("cli.learn.description"))} {
        root_->require_subcommand(0, 1);
        root_->add_option("--course", course_dir_, tr("cli.learn.course"));
        root_->add_option("--progress", progress_file_, tr("cli.learn.progress"));
        root_->add_option("--variant", variant_once_, tr("cli.learn.variant_once"))->check(CLI::Range(1, 999));
        const auto child = [&](const char* name, const char* help) {
            auto* c = root_->add_subcommand(name, tr(help));
            c->fallthrough();
            return c;
        };
        list_ = child("list", "cli.learn.list");
        list_->add_flag("--json", json_, tr("cli.json.description"));
        show_ = child("show", "cli.learn.show");
        show_->add_option("id", id_, tr("cli.learn.id"))->required();
        show_->add_flag("--json", json_, tr("cli.json.description"));
        start_ = child("start", "cli.learn.start");
        start_->add_option("id", id_, tr("cli.learn.id"))->required();
        start_->add_option("-o,--output", output_, tr("cli.learn.output"));
        check_ = child("check", "cli.learn.check");
        check_->add_option("id", id_, tr("cli.learn.id"))->required();
        check_->add_option("file", file_, tr("cli.learn.file"));
        check_->add_option("--answer", answer_, tr("cli.learn.answer"));
        check_->add_flag("--json", json_, tr("cli.json.description"));
        hint_ = child("hint", "cli.learn.hint");
        hint_->add_option("id", id_, tr("cli.learn.id"))->required();
        solution_ = child("solution", "cli.learn.solution");
        solution_->add_option("id", id_, tr("cli.learn.id"))->required();
        solution_->add_flag("--yes", yes_, tr("cli.learn.yes"));
        variant_ = child("variant", "cli.learn.variant");
        variant_->add_option("number", variant_set_, tr("cli.learn.variant_number"))->check(CLI::Range(1, 999));
        variant_->add_option("--student", student_, tr("cli.learn.student"));
        report_ = child("report", "cli.learn.report");
        report_->add_option("-o,--output", output_, tr("cli.learn.report_output"));
        lint_ = child("lint", "cli.learn.lint");
        lint_->add_option("--variants", lint_variants_, tr("cli.learn.lint_variants"))->check(CLI::Range(1, 999));
    }

    bool parsed() const override { return root_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        if (!load_course(err)) return 1;
        if (lint_->parsed()) return lint(out);
        const auto file = progress_path_now();
        auto loaded = load_progress(file);
        if (loaded.warning) err << *loaded.warning << '\n';
        progress_ = std::move(loaded.progress);
        if (variant_->parsed()) return variant(out, err);
        variant_used_ = variant_once_.value_or(progress_.variant);
        if (report_->parsed()) return report(out, err);
        if (list_->parsed() || !id_given()) return list(out);
        const auto* t = course_->find(id_);
        if (!t) {
            err << say("cli.learn.no_task", fmt::arg("id", id_)) << '\n';
            return 2;
        }
        if (show_->parsed()) return show(*t, out);
        if (start_->parsed()) return start(*t, out, err);
        if (check_->parsed()) return check(*t, out, err);
        if (hint_->parsed()) return hint(*t, out, err);
        return solution(*t, out, err);
    }

private:
    bool id_given() const { return show_->parsed() || start_->parsed() || check_->parsed() || hint_->parsed() || solution_->parsed(); }

    bool load_course(std::ostream& err) {
        if (course_dir_.empty()) {
            course_ = &builtin_course();
            return true;
        }
        auto files = read_course_dir(course_dir_);
        if (!files) {
            err << files.error().message << '\n';
            return false;
        }
        auto c = learn::load_course(*files);
        if (!c) {
            err << c.error().message << '\n';
            return false;
        }
        own_course_ = std::move(*c);
        course_ = &*own_course_;
        return true;
    }

    std::filesystem::path progress_path_now() const {
        if (!progress_file_.empty()) return progress_file_;
        const auto dir = io::config_dir(io::process_env()).value_or(std::filesystem::path{"."} / ".config" / "signals");
        return learn::progress_path(dir);
    }

    std::filesystem::path config_dir() const { return progress_path_now().parent_path(); }

    bool save(std::ostream& err) const {
        if (auto r = save_progress(progress_path_now(), progress_); !r) {
            err << r.error().message << '\n';
            return false;
        }
        return true;
    }

    TaskState state_of(const Task& t) const {
        const auto it = progress_.tasks.find(t.id);
        return it == progress_.tasks.end() ? TaskState::open : it->second.state;
    }

    int hints_of(const Task& t) const {
        const auto it = progress_.tasks.find(t.id);
        return it == progress_.tasks.end() ? 0 : it->second.hints;
    }

    std::size_t solved_in(const Chapter& ch) const {
        std::size_t n = 0;
        for (const auto& t : ch.tasks) n += state_of(t) != TaskState::open;
        return n;
    }

    int list(std::ostream& out) const {
        if (json_) {
            nlohmann::json j{{"course", course_->title()}, {"variant", variant_used_}, {"active", progress_.active}, {"chapters", nlohmann::json::array()}};
            for (const auto& ch : course_->chapters) {
                nlohmann::json c{{"id", ch.id}, {"title", ch.title()}, {"tasks", nlohmann::json::array()}};
                for (const auto& t : ch.tasks)
                    c["tasks"].push_back({{"id", t.id}, {"title", t.text().title}, {"kind", t.kind == TaskKind::graph ? "graph" : "answer"},
                                          {"state", state_name(state_of(t))}});
                j["chapters"].push_back(std::move(c));
            }
            out << j.dump(2) << '\n';
            return 0;
        }
        std::size_t solved = 0;
        for (const auto& ch : course_->chapters) solved += solved_in(ch);
        out << say("cli.learn.list_head", fmt::arg("course", course_->title()), fmt::arg("variant", variant_used_), fmt::arg("solved", solved),
                   fmt::arg("total", course_->size()))
            << "\n";
        for (const auto& ch : course_->chapters) {
            out << fmt::format("\n{}. {}  {}/{}\n", ch.id, ch.title(), solved_in(ch), ch.tasks.size());
            for (const auto& t : ch.tasks)
                out << fmt::format("  {} {} {} {}\n", t.id == progress_.active ? "●" : " ", mark(state_of(t)), t.id, t.text().title);
        }
        return 0;
    }

    int show(const Task& t, std::ostream& out) const {
        const auto& text = t.text();
        const auto opened = std::min<std::size_t>(static_cast<std::size_t>(hints_of(t)), text.hints.size());
        if (json_) {
            nlohmann::json j{{"id", t.id}, {"title", text.title}, {"kind", t.kind == TaskKind::graph ? "graph" : "answer"},
                             {"statement", render_text(text.statement, t, variant_used_)}, {"goals", nlohmann::json::array()}, {"hints", nlohmann::json::array()}};
            if (t.answer && t.answer->type == AnswerType::choice) j["options"] = variant_answer(t, variant_used_).options;
            for (const auto& g : text.goals) j["goals"].push_back(render_text(g, t, variant_used_));
            for (std::size_t i = 0; i < opened; ++i) j["hints"].push_back(render_text(text.hints[i], t, variant_used_));
            out << j.dump(2) << '\n';
            return 0;
        }
        out << t.id << " " << text.title << "\n\n" << render_text(text.statement, t, variant_used_) << "\n";
        if (const auto options = options_markdown(t, variant_used_); !options.empty()) out << "\n" << options;
        if (!text.goals.empty()) {
            out << "\n" << say("cli.learn.goals") << "\n";
            for (std::size_t i = 0; i < text.goals.size(); ++i) out << fmt::format("  {}. {}\n", i + 1, render_text(text.goals[i], t, variant_used_));
        }
        print_hints(t, opened, out);
        out << "\n" << say(t.kind == TaskKind::graph ? "cli.learn.next_graph" : "cli.learn.next_answer", fmt::arg("id", t.id)) << "\n";
        return 0;
    }

    void print_hints(const Task& t, std::size_t opened, std::ostream& out) const {
        const auto& hints = t.text().hints;
        out << "\n" << say("cli.learn.hints", fmt::arg("open", opened), fmt::arg("total", hints.size())) << "\n";
        for (std::size_t i = 0; i < opened; ++i) out << fmt::format("  {}. {}\n", i + 1, render_text(hints[i], t, variant_used_));
    }

    int start(const Task& t, std::ostream& out, std::ostream& err) {
        progress_.active = t.id;
        if (!save(err)) return 1;
        if (t.kind == TaskKind::answer || t.start_sig.empty()) {
            out << render_text(t.text().statement, t, variant_used_) << "\n\n";
            if (const auto options = options_markdown(t, variant_used_); !options.empty()) out << options << "\n";
            out << say("cli.learn.next_answer", fmt::arg("id", t.id)) << "\n";
            if (t.start_sig.empty()) return 0;
        }
        const std::filesystem::path file = output_.empty() ? std::filesystem::path{t.id + ".sig"} : std::filesystem::path{output_};
        std::error_code ec;
        if (std::filesystem::exists(file, ec)) {
            out << say("cli.learn.kept", fmt::arg("file", file.string())) << "\n";
            return 0;
        }
        if (auto r = io::write_file_atomic(file, start_graph(t, variant_used_)); !r) {
            err << r.error().message << '\n';
            return 1;
        }
        out << say("cli.learn.written", fmt::arg("file", file.string()), fmt::arg("id", t.id)) << "\n";
        return 0;
    }

    int check(const Task& t, std::ostream& out, std::ostream& err) {
        Submission s;
        std::filesystem::path file;
        if (t.kind == TaskKind::graph) {
            file = file_.empty() ? std::filesystem::path{t.id + ".sig"} : std::filesystem::path{file_};
            auto text = read_text(file);
            if (!text) {
                err << say("cli.learn.no_file", fmt::arg("file", file.string()), fmt::arg("id", t.id)) << '\n';
                return 1;
            }
            s.sig = std::move(*text);
            s.base_dir = std::filesystem::absolute(file).parent_path();
        } else {
            if (answer_.empty()) {
                err << say("cli.learn.need_answer", fmt::arg("id", t.id)) << '\n';
                return 2;
            }
            s.answer = answer_;
        }
        core::CancelToken cancel;
        auto r = check_task(t, variant_used_, s, cancel);
        if (!r) {
            err << r.error().message << '\n';
            return 1;
        }
        record_check(progress_, t.id, *r, answer_, now_text(), variant_used_);
        progress_.active = t.id;
        if (r->passed && t.kind == TaskKind::graph) {
            const auto keep = solution_path(config_dir(), t.id);
            std::error_code ec;
            std::filesystem::create_directories(keep.parent_path(), ec);
            if (auto w = io::write_file_atomic(keep, *s.sig); !w) err << w.error().message << '\n';
        }
        if (!save(err)) return 1;
        const auto& goals = t.text().goals;
        if (json_) {
            nlohmann::json j{{"id", t.id}, {"passed", r->passed}, {"inputs", r->inputs}, {"seconds", r->seconds}, {"goals", nlohmann::json::array()}};
            if (r->error) j["error"] = *r->error;
            for (std::size_t i = 0; i < r->goals.size(); ++i) {
                nlohmann::json g{{"passed", r->goals[i].passed}, {"detail", r->goals[i].detail}};
                if (i < goals.size()) g["text"] = render_text(goals[i], t, variant_used_);
                if (r->goals[i].value) g["value"] = *r->goals[i].value;
                j["goals"].push_back(std::move(g));
            }
            out << j.dump(2) << '\n';
            return r->passed ? 0 : r->error ? 1 : 3;
        }
        out << say(r->passed ? "cli.learn.passed" : "cli.learn.failed", fmt::arg("id", t.id), fmt::arg("title", t.text().title)) << "\n";
        if (r->error) out << "  " << *r->error << "\n";
        for (std::size_t i = 0; i < r->goals.size(); ++i) {
            const auto& g = r->goals[i];
            const auto text = i < goals.size() ? render_text(goals[i], t, variant_used_) : say("cli.learn.answer_goal");
            const auto mark_text = r->error && !g.passed && g.detail.empty() ? "·" : g.passed ? "✓" : "✗";
            out << fmt::format("  {} {}", mark_text, text);
            if (!g.detail.empty()) out << " — " << g.detail;
            out << "\n";
        }
        if (t.kind == TaskKind::graph && !r->error) out << say("cli.learn.inputs", fmt::arg("n", r->inputs), fmt::arg("seconds", fmt::format("{:.2f}", r->seconds))) << "\n";
        return r->passed ? 0 : r->error ? 1 : 3;
    }

    int hint(const Task& t, std::ostream& out, std::ostream& err) {
        auto& tp = progress_.tasks[t.id];
        const auto total = t.text().hints.size();
        const bool more = static_cast<std::size_t>(tp.hints) < total;
        if (more) ++tp.hints;
        if (!save(err)) return 1;
        print_hints(t, static_cast<std::size_t>(tp.hints), out);
        if (!more) out << say("cli.learn.no_more_hints") << "\n";
        return 0;
    }

    int solution(const Task& t, std::ostream& out, std::ostream& err) {
        if (state_of(t) == TaskState::open) {
            if (!yes_) {
                out << say("cli.learn.confirm_solution", fmt::arg("id", t.id)) << "\n";
                return 2;
            }
            reveal(progress_, t.id);
            if (!save(err)) return 1;
        }
        out << t.id << " " << t.text().title << "\n\n" << render_text(t.text().solution, t, variant_used_) << "\n";
        return 0;
    }

    int variant(std::ostream& out, std::ostream& err) {
        if (variant_set_ || !student_.empty()) {
            if (variant_set_) progress_.variant = *variant_set_;
            if (!student_.empty()) progress_.student = student_;
            if (!save(err)) return 1;
        }
        out << say("cli.learn.variant_now", fmt::arg("variant", progress_.variant),
                   fmt::arg("student", progress_.student.empty() ? std::string{"—"} : progress_.student))
            << "\n";
        return 0;
    }

    int report(std::ostream& out, std::ostream& err) const {
        auto shown = progress_;
        shown.variant = variant_used_;
        const auto dir = config_dir();
        const auto text = progress_report(*course_, shown, core::version(), now_text().substr(0, 10),
                                          [&](std::string_view id) { return read_text(solution_path(dir, id)); });
        if (output_.empty()) {
            out << text;
            return 0;
        }
        if (auto r = io::write_file_atomic(output_, text); !r) {
            err << r.error().message << '\n';
            return 1;
        }
        out << say("cli.learn.report_written", fmt::arg("file", output_)) << "\n";
        return 0;
    }

    int lint(std::ostream& out) const {
        std::vector<int> variants;
        for (int v = 1; v <= lint_variants_; ++v) variants.push_back(v);
        const auto issues = lint_course(*course_, variants);
        for (const auto& i : issues) out << i.task << ": " << i.message << "\n";
        out << say(issues.empty() ? "cli.learn.lint_ok" : "cli.learn.lint_bad", fmt::arg("tasks", course_->size()), fmt::arg("issues", issues.size()))
            << "\n";
        return issues.empty() ? 0 : 1;
    }

    CLI::App* root_;
    CLI::App *list_, *show_, *start_, *check_, *hint_, *solution_, *variant_, *report_, *lint_;
    std::string course_dir_, progress_file_, id_, output_, file_, answer_, student_;
    std::optional<int> variant_once_, variant_set_;
    int lint_variants_ = 3;
    bool json_ = false, yes_ = false;
    const Course* course_ = nullptr;
    std::optional<Course> own_course_;
    Progress progress_;
    int variant_used_ = 1;
};

}  // namespace

void add_learn_commands(CLI::App& app, Commands& commands) { commands.push_back(std::make_unique<LearnCommand>(app)); }

}  // namespace signals::cli
