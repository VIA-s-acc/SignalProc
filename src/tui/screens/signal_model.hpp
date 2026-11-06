#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "audio/player.hpp"
#include "core/memory.hpp"
#include "io/stats.hpp"
#include "io/wav.hpp"
#include "tui/data/pyramid.hpp"
#include "tui/jobs/jobs.hpp"

namespace signals::tui {

struct SignalData {
    std::filesystem::path path;
    io::WavInfo info;
    std::vector<io::ChannelStats> stats;
    std::vector<std::vector<float>> samples;
    std::vector<MinMaxPyramid> pyramids;
    std::vector<double> psd_db, psd_hz;
    std::vector<double> band_energy;
    std::vector<std::size_t> band_lengths;
    std::size_t full_scale_samples = 0;
    core::TrackedBytes memory;
    core::TrackedBytes display_memory;

    double duration() const { return info.sample_rate ? static_cast<double>(info.frames) / info.sample_rate : 0.0; }
};

struct LogLine {
    enum Level { info, warn, error };
    std::string time;
    std::string text;
    Level level = info;
};

struct SelectionStats {
    double duration = 0, rms_dbfs = 0, peak_dbfs = 0, dc = 0, crest_db = 0;
};

class SignalModel {
public:
    explicit SignalModel(std::function<std::string()> clock = {}, std::size_t memory_budget = 0);

    void open(const std::filesystem::path& path, JobRunner& jobs);
    std::shared_ptr<const SignalData> data() const;
    std::vector<LogLine> log() const;
    std::optional<std::uint64_t> job() const;

    double view_begin() const;
    double view_end() const;
    double amplitude() const;
    void zoom(double factor, double anchor_seconds);
    void zoom_amplitude(double factor);
    void pan(double delta_seconds);

    std::optional<double> cursor_a() const;
    std::optional<double> cursor_b() const;
    void set_cursor_a(double t);
    void set_cursor_b(double t);
    std::optional<std::pair<double, double>> selection() const;
    void select(double t0, double t1);
    void clear_selection();
    void compute_selection(JobRunner& jobs);
    std::vector<SelectionStats> selection_stats() const;

    std::optional<audio::Clip> clip() const;
    void add_log(std::string text, LogLine::Level level);

private:
    void publish(std::shared_ptr<const SignalData> d);
    double sync_view() const;
    double clamp_time(double t) const;

    std::function<std::string()> clock_;
    std::size_t memory_budget_;
    mutable std::mutex mutex_;
    std::shared_ptr<const SignalData> data_;
    std::vector<LogLine> log_;
    std::optional<std::uint64_t> job_;
    std::vector<SelectionStats> selection_stats_;
    mutable std::shared_ptr<const SignalData> view_data_;
    mutable double begin_ = 0, end_ = 0, amplitude_ = 1;
    mutable std::optional<double> cursor_a_, cursor_b_;
    mutable std::optional<std::pair<double, double>> selection_;
};

}  // namespace signals::tui
