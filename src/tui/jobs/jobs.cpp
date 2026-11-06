#include "core/thread_name.hpp"
#include "tui/jobs/jobs.hpp"

#include <algorithm>
#include <exception>

namespace signals::tui {

namespace detail {

struct Job {
    JobView view;
    JobFn fn;
    std::atomic<bool> cancel{false};
};

}  // namespace detail

namespace {

constexpr std::size_t kHistory = 20;

double seconds_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

}  // namespace

void JobContext::begin(std::size_t stage) {
    {
        std::lock_guard lock{runner_.mutex_};
        if (stage >= job_.view.stages.size()) return;
        stage_ = stage;
        started_ = std::chrono::steady_clock::now();
        job_.view.stages[stage].state = StageState::running;
        job_.view.stages[stage].fraction = 0;
    }
    runner_.changed(false);
}

void JobContext::finish(std::size_t bytes, std::string detail) {
    {
        std::lock_guard lock{runner_.mutex_};
        if (!stage_) return;
        auto& s = job_.view.stages[*stage_];
        s.state = StageState::done;
        s.fraction = 1;
        s.seconds = seconds_since(started_);
        s.bytes = bytes;
        s.detail = std::move(detail);
        stage_.reset();
    }
    runner_.changed(false);
}

void JobContext::fail(std::string message) {
    {
        std::lock_guard lock{runner_.mutex_};
        if (stage_) {
            auto& s = job_.view.stages[*stage_];
            s.state = StageState::failed;
            s.seconds = seconds_since(started_);
            stage_.reset();
        }
        job_.view.error = std::move(message);
    }
    runner_.changed(false);
}

void JobContext::report(double fraction) {
    {
        std::lock_guard lock{runner_.mutex_};
        if (!stage_) return;
        job_.view.stages[*stage_].fraction = std::clamp(fraction, 0.0, 1.0);
        job_.view.stages[*stage_].seconds = seconds_since(started_);
    }
    runner_.changed(false);
}

bool JobContext::cancelled() const { return job_.cancel.load(); }

JobRunner::JobRunner(std::function<void()> notify, std::chrono::milliseconds min_interval, bool inline_mode)
    : notify_{std::move(notify)}, min_interval_{min_interval}, inline_{inline_mode} {
    if (!inline_) worker_ = std::thread{[this] { loop(); }};
}

JobRunner::~JobRunner() {
    {
        std::lock_guard lock{mutex_};
        stop_ = true;
        queue_.clear();
        if (current_) current_->cancel = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

std::uint64_t JobRunner::submit(std::string title, std::vector<std::string> stages, JobFn fn) {
    auto job = std::make_shared<detail::Job>();
    job->fn = std::move(fn);
    job->view.title = std::move(title);
    for (auto& s : stages) job->view.stages.push_back({std::move(s)});
    std::uint64_t id;
    {
        std::lock_guard lock{mutex_};
        id = job->view.id = next_id_++;
        if (!inline_) queue_.push_back(job);
    }
    if (inline_) execute(job);
    else wake_.notify_one();
    return id;
}

void JobRunner::cancel(std::uint64_t id) {
    std::shared_ptr<detail::Job> dropped;
    {
        std::lock_guard lock{mutex_};
        if (current_ && current_->view.id == id) current_->cancel = true;
        auto it = std::find_if(queue_.begin(), queue_.end(), [id](const auto& j) { return j->view.id == id; });
        if (it != queue_.end()) {
            dropped = *it;
            queue_.erase(it);
        }
    }
    if (dropped) {
        dropped->cancel = true;
        retire(dropped);
    }
}

void JobRunner::cancel_all() {
    std::deque<std::shared_ptr<detail::Job>> dropped;
    {
        std::lock_guard lock{mutex_};
        if (current_) current_->cancel = true;
        dropped.swap(queue_);
    }
    for (auto& j : dropped) {
        j->cancel = true;
        retire(j);
    }
}

std::vector<JobView> JobRunner::snapshot() const {
    std::lock_guard lock{mutex_};
    std::vector<JobView> out(history_.begin(), history_.end());
    if (current_) out.push_back(current_->view);
    for (const auto& j : queue_) out.push_back(j->view);
    return out;
}

std::size_t JobRunner::queued() const {
    std::lock_guard lock{mutex_};
    return queue_.size();
}

void JobRunner::loop() {
    core::set_thread_name("signals-jobs");
    for (;;) {
        std::shared_ptr<detail::Job> job;
        {
            std::unique_lock lock{mutex_};
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = queue_.front();
            queue_.pop_front();
            current_ = job;
        }
        execute(job);
    }
}

void JobRunner::execute(const std::shared_ptr<detail::Job>& job) {
    JobContext ctx{*this, *job};
    try {
        if (!job->cancel) job->fn(ctx);
    } catch (const std::exception& e) {
        ctx.fail(e.what());
    } catch (...) {
        ctx.fail("unknown error");
    }
    if (ctx.stage_) {
        if (job->cancel) {
            std::lock_guard lock{mutex_};
            auto& stage = job->view.stages[*ctx.stage_];
            if (stage.state == StageState::running) stage.state = StageState::skipped;
        } else {
            ctx.finish();
        }
    }
    retire(job);
}

void JobRunner::retire(const std::shared_ptr<detail::Job>& job) {
    bool stopping;
    {
        std::lock_guard lock{mutex_};
        for (auto& s : job->view.stages)
            if (s.state == StageState::waiting) s.state = StageState::skipped;
        job->view.finished = true;
        job->view.cancelled = job->cancel.load();
        history_.push_back(job->view);
        while (history_.size() > kHistory) history_.pop_front();
        if (current_ == job) current_.reset();
        stopping = stop_;
    }
    if (!stopping) changed(true);
}

void JobRunner::changed(bool force) {
    {
        std::lock_guard lock{mutex_};
        if (stop_) return;
    }
    std::lock_guard lock{notify_mutex_};
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - last_notify_ < min_interval_) return;
    last_notify_ = now;
    if (notify_) notify_();
}

}  // namespace signals::tui
