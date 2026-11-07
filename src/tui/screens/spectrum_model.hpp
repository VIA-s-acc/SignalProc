#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/memory.hpp"
#include "core/spectral.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/signal_model.hpp"

namespace signals::tui {

struct SpectrumOptions {
    std::size_t n = 4096;
    std::size_t hop_divisor = 4;
    int channel = 0;
    friend bool operator==(const SpectrumOptions&, const SpectrumOptions&) = default;
};

struct SpectrogramData {
    SpectrumOptions options;
    double fs = 0, duration = 0;
    std::size_t bins = 0, cols = 0, hop = 0;
    std::vector<float> power;
    std::vector<double> col_time;
    float peak = 0;
    std::weak_ptr<const SignalData> source;
    core::TrackedBytes memory;
};

struct SelectionSpectrum {
    double t0 = 0, t1 = 0;
    SpectrumOptions options;
    std::vector<double> f, db;
    std::vector<core::Peak> peaks;
    std::optional<double> f0;
    std::vector<core::Harmonic> harmonics;
    std::weak_ptr<const SignalData> source;
};

struct GridSpec {
    std::size_t rows = 0, cols = 0;
    bool log = true;
    double fmin = 30, floor_db = -90;
    double t0 = 0, t1 = 0;
    friend bool operator==(const GridSpec&, const GridSpec&) = default;
};

std::vector<double> spectrogram_grid(const SpectrogramData& s, const GridSpec& g);
double box_level_db(const SpectrogramData& s, double t0, double t1, double f0, double f1);
double level_at_db(const SpectrogramData& s, double t, double f);

class SpectrumModel {
public:
    explicit SpectrumModel(std::shared_ptr<SignalModel> source);

    void sync(JobRunner& jobs);
    void set_options(SpectrumOptions o, JobRunner& jobs);
    SpectrumOptions options() const { return options_; }
    void set_selection(double t0, double t1, JobRunner& jobs);
    std::pair<double, double> selection_range() const;
    std::shared_ptr<const SpectrogramData> spectrogram() const;
    std::shared_ptr<const SelectionSpectrum> selection() const;
    std::optional<std::string> error() const;
    std::optional<std::uint64_t> job() const;
    void cancel(JobRunner& jobs);
    std::shared_ptr<const SignalData> source() const { return source_->data(); }

private:
    void compute(JobRunner& jobs);
    void compute_selection(JobRunner& jobs);

    std::shared_ptr<SignalModel> source_;
    SpectrumOptions options_;
    std::optional<std::pair<double, double>> requested_;
    std::shared_ptr<const SignalData> seen_;
    std::optional<std::pair<double, double>> selection_for_;
    mutable std::mutex mutex_;
    std::shared_ptr<const SpectrogramData> data_;
    std::shared_ptr<const SelectionSpectrum> selection_;
    std::optional<std::string> error_;
    std::optional<std::uint64_t> job_, selection_job_;
    std::uint64_t gen_ = 0, done_ = 0, selection_gen_ = 0, selection_done_ = 0;
};

}  // namespace signals::tui
