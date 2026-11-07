#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio/player.hpp"
#include "core/extension.hpp"
#include "core/process/shrink.hpp"
#include "core/process/threshold.hpp"
#include "core/progress.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/signal_model.hpp"

namespace signals::tui {

struct ProcessingOptions {
    std::string wavelet = "sym8";
    std::size_t levels = 5;
    core::Extension mode = core::Extension::symmetric;
    core::ShrinkRule rule = core::ShrinkRule::bayes;
    core::ThresholdMode threshold = core::ThresholdMode::soft;
    double scale = 1.0;
    std::size_t start = 100'000, length = 8192;
    int channel = 0;
    double snr_db = 10;
    std::vector<double> gains = {};
    friend bool operator==(const ProcessingOptions&, const ProcessingOptions&) = default;
};

struct BandResult {
    std::string name;
    double lo_hz = 0, hi_hz = 0, max_abs = 0, lambda = 0;
    std::size_t length = 0, kept = 0;
    std::vector<double> coeffs;
};

struct CurvePoint {
    std::size_t k = 0;
    double err_db = 0;
};

struct TransferCurves {
    std::vector<double> omega, t_db, a_db;
};

struct ProcessingResult {
    ProcessingOptions options;
    double fs = 0;
    std::vector<double> clean, noisy, out, residual;
    std::vector<BandResult> bands;
    double sigma = 0, snr_in = 0, snr_out = 0, mse = 0, energy_pct = 0, err_db = 0;
    std::size_t kept = 0, total = 0;
    std::vector<CurvePoint> curve;
    TransferCurves transfer;
    std::vector<float> audio_noisy, audio_out, audio_residual;
    std::weak_ptr<const SignalData> source;
};

core::Result<ProcessingResult> process_fragment(std::span<const double> clean, std::span<const double> noisy, double fs, const ProcessingOptions& o,
                                                core::ProgressSink* progress = nullptr);
core::Result<TransferCurves> band_transfer(std::string_view wavelet, std::size_t levels, std::span<const double> gains, std::size_t np,
                                           std::span<const std::size_t> bins);
std::vector<std::size_t> transfer_bins(std::size_t np);
std::vector<double> seeded_noise(std::size_t n, double sigma, std::uint64_t seed);

class ProcessingModel {
public:
    explicit ProcessingModel(std::shared_ptr<SignalModel> source);

    void sync(JobRunner& jobs);
    void set_options(ProcessingOptions o, JobRunner& jobs);
    ProcessingOptions options() const { return options_; }
    std::shared_ptr<const ProcessingResult> result() const;
    std::optional<std::string> error() const;
    std::optional<std::uint64_t> job() const;
    void cancel(JobRunner& jobs);
    std::shared_ptr<const SignalData> source() const { return source_->data(); }
    enum class Clip { noisy, out, residual };
    std::optional<audio::Clip> clip(Clip kind) const;
    void undo(JobRunner& jobs);

private:
    void compute(JobRunner& jobs);

    std::shared_ptr<SignalModel> source_;
    ProcessingOptions options_;
    std::optional<ProcessingOptions> previous_;
    std::shared_ptr<const SignalData> seen_;
    mutable std::mutex mutex_;
    std::shared_ptr<const ProcessingResult> result_;
    std::optional<std::string> error_;
    std::optional<std::uint64_t> job_;
    std::uint64_t gen_ = 0, done_ = 0;
};

}  // namespace signals::tui
