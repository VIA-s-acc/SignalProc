#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "core/progress.hpp"
#include "learn/checker.hpp"
#include "learn/progress.hpp"
#include "learn/task.hpp"

namespace signals::tui {

struct UiContext;
struct FinishedRun;
class PipelineModel;

struct LearnView {
    learn::Progress progress = {};
    std::optional<learn::CheckResult> last = {};
    bool checking = false;
    std::optional<std::string> warning = {};
    std::optional<std::string> message = {};
};

class LearnState {
public:
    LearnState(const learn::Course& course, std::filesystem::path config_dir, std::shared_ptr<PipelineModel> pipeline);

    LearnView view() const;
    const learn::Course& course() const { return course_; }
    const std::filesystem::path& config_dir() const { return dir_; }
    const learn::Task* active_task() const;
    int variant() const;

    void select(std::string_view id);
    void start(UiContext& ui);
    void check(UiContext& ui);
    void submit_answer(UiContext& ui, std::string text);
    void on_run_finished(const FinishedRun& run, UiContext& ui);
    bool next_hint();
    void reveal();
    void cancel();
    void set_variant(int v);
    void set_student(std::string name);
    void clear_message();
    core::Result<std::filesystem::path> write_report(const std::filesystem::path& out) const;

private:
    void run_check(UiContext& ui, const learn::Task& t, learn::Submission s, std::string answer);
    void save_locked();

    const learn::Course& course_;
    std::filesystem::path dir_;
    std::shared_ptr<PipelineModel> pipeline_;
    mutable std::mutex mutex_;
    learn::Progress progress_;
    std::optional<learn::CheckResult> last_;
    bool checking_ = false;
    std::optional<std::string> warning_, message_;
    std::shared_ptr<core::CancelToken> cancel_;
};

}  // namespace signals::tui
