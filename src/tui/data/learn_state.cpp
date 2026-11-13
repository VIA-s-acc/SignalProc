#include "tui/data/learn_state.hpp"

#include <fstream>
#include <sstream>

#include "core/version.hpp"
#include "i18n/i18n.hpp"
#include "io/temp_file.hpp"
#include "learn/report.hpp"
#include "pipeline/sig.hpp"
#include "tui/screens/pipeline_model.hpp"
#include "tui/shell/screen.hpp"

namespace signals::tui {

namespace {

std::optional<std::string> read_file(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

}  // namespace

LearnState::LearnState(const learn::Course& course, std::filesystem::path config_dir, std::shared_ptr<PipelineModel> pipeline)
    : course_{course}, dir_{std::move(config_dir)}, pipeline_{std::move(pipeline)} {
    auto loaded = learn::load_progress(learn::progress_path(dir_));
    progress_ = std::move(loaded.progress);
    warning_ = std::move(loaded.warning);
}

LearnView LearnState::view() const {
    std::lock_guard lock{mutex_};
    return {progress_, last_, checking_, warning_, message_};
}

const learn::Task* LearnState::active_task() const {
    std::lock_guard lock{mutex_};
    return course_.find(progress_.active);
}

int LearnState::variant() const {
    std::lock_guard lock{mutex_};
    return progress_.variant;
}

void LearnState::save_locked() {
    if (auto r = learn::save_progress(learn::progress_path(dir_), progress_); !r) message_ = r.error().message;
}

void LearnState::select(std::string_view id) {
    std::lock_guard lock{mutex_};
    if (progress_.active != id) last_.reset();
    progress_.active = std::string{id};
    message_.reset();
    save_locked();
}

void LearnState::start(UiContext& ui) {
    const auto* t = active_task();
    if (!t) return;
    if (t->start_sig.empty()) {
        if (ui.navigate) ui.navigate(t->open, {});
        return;
    }
    const auto work = learn::work_path(dir_, t->id);
    std::error_code ec;
    if (!std::filesystem::exists(work, ec)) {
        std::filesystem::create_directories(work.parent_path(), ec);
        if (auto r = io::write_file_atomic(work, learn::start_graph(*t, variant())); !r) {
            std::lock_guard lock{mutex_};
            message_ = r.error().message;
            return;
        }
    }
    if (ui.open_graph) ui.open_graph(work);
}

void LearnState::check(UiContext& ui) {
    const auto* t = active_task();
    if (!t) return;
    if (t->kind == learn::TaskKind::answer) {
        std::lock_guard lock{mutex_};
        message_ = tr("tui.learn.type_answer");
        return;
    }
    const auto work = learn::work_path(dir_, t->id);
    learn::Submission s;
    s.base_dir = work.parent_path();
    if (pipeline_ && pipeline_->path() && *pipeline_->path() == work) s.sig = pipeline::write_sig(pipeline_->doc());
    else if (auto text = read_file(work)) s.sig = std::move(*text);
    if (!s.sig) {
        std::lock_guard lock{mutex_};
        message_ = tr("tui.learn.start_first");
        return;
    }
    run_check(ui, *t, std::move(s), {});
}

void LearnState::submit_answer(UiContext& ui, std::string text) {
    const auto* t = active_task();
    if (!t || t->kind != learn::TaskKind::answer) return;
    learn::Submission s;
    s.answer = text;
    run_check(ui, *t, std::move(s), std::move(text));
}

void LearnState::on_run_finished(const FinishedRun& run, UiContext& ui) {
    const auto* t = active_task();
    if (!t || t->kind != learn::TaskKind::graph || !run.ok || !run.path || *run.path != learn::work_path(dir_, t->id)) return;
    learn::Submission s;
    s.sig = run.sig;
    s.base_dir = run.path->parent_path();
    run_check(ui, *t, std::move(s), {});
}

void LearnState::run_check(UiContext& ui, const learn::Task& t, learn::Submission s, std::string answer) {
    std::shared_ptr<core::CancelToken> token;
    int variant = 1;
    {
        std::lock_guard lock{mutex_};
        if (checking_) return;
        checking_ = true;
        message_.reset();
        token = cancel_ = std::make_shared<core::CancelToken>();
        variant = progress_.variant;
    }
    auto work = [this, &t, s = std::move(s), answer = std::move(answer), token, variant] {
        auto r = learn::check_task(t, variant, s, *token);
        std::lock_guard lock{mutex_};
        checking_ = false;
        if (!r) {
            message_ = r.error().code == core::Errc::cancelled ? tr("tui.learn.cancelled") : r.error().message;
            return;
        }
        if (progress_.active == t.id && progress_.variant == variant) last_ = *r;
        learn::record_check(progress_, t.id, *r, answer, learn::now_text());
        if (r->passed && s.sig) {
            const auto keep = learn::solution_path(dir_, t.id);
            std::error_code ec;
            std::filesystem::create_directories(keep.parent_path(), ec);
            (void)io::write_file_atomic(keep, *s.sig);
        }
        save_locked();
    };
    if (!ui.jobs) {
        work();
        return;
    }
    ui.jobs->submit(tr("tui.learn.job"), {tr("tui.learn.job")}, [work = std::move(work)](JobContext& ctx) mutable {
        ctx.begin(0);
        work();
        ctx.finish();
    });
}

bool LearnState::next_hint() {
    std::lock_guard lock{mutex_};
    const auto* t = course_.find(progress_.active);
    if (!t) return false;
    auto& tp = progress_.tasks[t->id];
    if (static_cast<std::size_t>(tp.hints) >= t->text().hints.size()) return false;
    ++tp.hints;
    save_locked();
    return true;
}

void LearnState::reveal() {
    std::lock_guard lock{mutex_};
    if (progress_.active.empty()) return;
    learn::reveal(progress_, progress_.active);
    save_locked();
}

void LearnState::cancel() {
    std::lock_guard lock{mutex_};
    if (cancel_) cancel_->cancel();
}

void LearnState::set_variant(int v) {
    std::lock_guard lock{mutex_};
    progress_.variant = v;
    last_.reset();
    save_locked();
}

void LearnState::set_student(std::string name) {
    std::lock_guard lock{mutex_};
    progress_.student = std::move(name);
    save_locked();
}

void LearnState::clear_message() {
    std::lock_guard lock{mutex_};
    message_.reset();
    warning_.reset();
}

core::Result<std::filesystem::path> LearnState::write_report(const std::filesystem::path& out) const {
    learn::Progress p;
    {
        std::lock_guard lock{mutex_};
        p = progress_;
    }
    const auto text = learn::progress_report(course_, p, core::version(), learn::now_text().substr(0, 10),
                                             [this](std::string_view id) { return read_file(learn::solution_path(dir_, id)); });
    if (auto r = io::write_file_atomic(out, text); !r) return std::unexpected{r.error()};
    return out;
}

}  // namespace signals::tui
