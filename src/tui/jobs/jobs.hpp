#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/progress.hpp"

namespace signals::tui {

enum class StageState { waiting, running, done, failed, skipped };

struct StageView {
    std::string name = {};
    StageState state = StageState::waiting;
    double fraction = 0;
    double seconds = 0;
    std::size_t bytes = 0;
    std::string detail = {};
};

struct JobView {
    std::uint64_t id = 0;
    std::string title = {};
    std::vector<StageView> stages = {};
    bool finished = false;
    bool cancelled = false;
    std::optional<std::string> error;
};

class JobRunner;

namespace detail {
struct Job;
}

class JobContext : public core::ProgressSink {
public:
    void begin(std::size_t stage);
    void finish(std::size_t bytes = 0, std::string detail = {});
    void fail(std::string message);
    void report(double fraction) override;
    bool cancelled() const override;

private:
    friend class JobRunner;
    JobContext(JobRunner& runner, detail::Job& job) : runner_{runner}, job_{job} {}

    JobRunner& runner_;
    detail::Job& job_;
    std::optional<std::size_t> stage_;
    std::chrono::steady_clock::time_point started_{};
};

using JobFn = std::function<void(JobContext&)>;

class JobRunner {
public:
    explicit JobRunner(std::function<void()> notify,
                       std::chrono::milliseconds min_interval = std::chrono::milliseconds{33}, bool inline_mode = false);
    ~JobRunner();
    JobRunner(const JobRunner&) = delete;
    JobRunner& operator=(const JobRunner&) = delete;

    std::uint64_t submit(std::string title, std::vector<std::string> stages, JobFn fn);
    void cancel(std::uint64_t id);
    void cancel_all();
    std::vector<JobView> snapshot() const;
    std::size_t queued() const;

private:
    friend class JobContext;
    void loop();
    void execute(const std::shared_ptr<detail::Job>& job);
    void retire(const std::shared_ptr<detail::Job>& job);
    void changed(bool force);

    std::function<void()> notify_;
    std::chrono::milliseconds min_interval_;
    bool inline_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::shared_ptr<detail::Job>> queue_;
    std::shared_ptr<detail::Job> current_;
    std::deque<JobView> history_;
    std::uint64_t next_id_ = 1;
    bool stop_ = false;
    std::mutex notify_mutex_;
    std::chrono::steady_clock::time_point last_notify_{};
    std::thread worker_;
};

}  // namespace signals::tui
