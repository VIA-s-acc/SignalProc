#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "audio/player.hpp"
#include "core/extension.hpp"
#include "core/packets.hpp"
#include "core/progress.hpp"
#include "core/memory.hpp"
#include "core/process/shrink.hpp"
#include "core/process/threshold.hpp"
#include "core/wavedec.hpp"
#include "tui/data/pyramid.hpp"
#include "tui/jobs/jobs.hpp"
#include "tui/screens/signal_model.hpp"

namespace signals::tui {

enum class Transform { dwt, swt, modwt };

struct DecompositionOptions {
    std::string wavelet = "db2";
    std::size_t levels = 6;
    core::Extension mode = core::Extension::symmetric;
    int channel = -1;
    Transform transform = Transform::dwt;
};

core::Result<core::BasicCoeffs<double>> forward_transform(const std::vector<double>& x, const core::FilterBank& bank, const DecompositionOptions& o);
core::Result<std::vector<double>> inverse_transform(const core::BasicCoeffs<double>& c, const core::FilterBank& bank, const DecompositionOptions& o);

struct BandInfo {
    std::string name;
    double lo_hz = 0, hi_hz = 0, energy_pct = 0;
    std::size_t length = 0;
};

struct Tiles {
    std::size_t rows = 0, cols = 0;
    std::vector<double> db;
};

struct DecompositionData {
    DecompositionOptions options;
    double fs = 0;
    std::vector<double> x;
    std::weak_ptr<const SignalData> source;
    std::vector<float> x_float;
    std::optional<MinMaxPyramid> x_pyramid;
    core::BasicCoeffs<double> coeffs;
    std::vector<BandInfo> bands;
    std::vector<std::vector<float>> band_abs;
    std::vector<MinMaxPyramid> band_pyramids;
    Tiles tiles;
    double pr_error = 0;
    double parseval_error = 0;
    double sigma = 0;
    bool orthogonal = false;
    core::TrackedBytes memory;
    core::TrackedBytes display_memory;
    double duration() const { return fs > 0 ? static_cast<double>(x.size()) / fs : 0.0; }
};

struct Processing {
    std::optional<core::ShrinkRule> rule;
    core::ThresholdMode mode = core::ThresholdMode::soft;
    std::size_t first_level = 1, last_level = 4;
    std::optional<double> keep_fraction;
    std::vector<double> gains_db;
};

struct Reconstruction {
    std::vector<bool> enabled;
    Processing processing;
    std::vector<double> lambdas;
    core::BasicCoeffs<double> processed;
    std::vector<float> xhat, difference;
    std::vector<std::vector<float>> components;
    std::optional<std::string> mra_skipped;
    std::optional<MinMaxPyramid> xhat_pyramid;
    std::vector<MinMaxPyramid> component_pyramids;
    double energy_kept_pct = 0;
    core::TrackedBytes memory;
};

struct PacketNodeInfo {
    core::PacketNode node;
    double lo_hz = 0, hi_hz = 0, energy_pct = 0, cost = 0;
};

struct PacketData {
    DecompositionOptions options;
    std::weak_ptr<const DecompositionData> source;
    core::PacketTree tree;
    std::vector<std::vector<PacketNodeInfo>> table;
    std::vector<double> level_cost;
    std::vector<core::PacketNode> best;
    double best_cost = 0, dwt_cost = 0, root_cost = 0, norm2 = 0;
    core::TrackedBytes memory;
    const PacketNodeInfo& info(core::PacketNode n) const { return table[n.level][core::freq_position(n)]; }
};

enum class MapKind { cwt, sst };

struct ScalogramData {
    MapKind kind = MapKind::cwt;
    std::size_t rows = 0, cols = 0;
    std::vector<double> db;
    std::vector<std::uint8_t> coi;
    std::vector<double> freq_hz;
    std::size_t decimation = 1;
    std::weak_ptr<const SignalData> signal;
    int channel = -1;
    core::TrackedBytes memory;
};

core::Result<ScalogramData> compute_scalogram(std::span<const double> x, double fs, std::size_t cols, core::ProgressSink* progress = nullptr,
                                              std::size_t budget = 0);
core::Result<ScalogramData> compute_synchrosqueezed(std::span<const double> x, double fs, std::size_t cols, core::ProgressSink* progress = nullptr,
                                                    std::size_t budget = 0);

enum class ClipKind { original, enabled, difference, band };

class DecompositionModel {
public:
    explicit DecompositionModel(std::shared_ptr<SignalModel> source, std::size_t memory_budget = 0);

    void sync(JobRunner& jobs);
    void set_options(DecompositionOptions o, JobRunner& jobs);
    DecompositionOptions options() const { return options_; }
    void toggle(std::size_t band, JobRunner& jobs);
    void set_enabled(std::vector<bool> enabled, JobRunner& jobs);
    void apply(Processing p, JobRunner& jobs);
    void undo(JobRunner& jobs);
    void cancel(JobRunner& jobs);
    void recompute(JobRunner& jobs);
    bool retry_cancelled(bool packets, MapKind kind = MapKind::cwt);
    bool cancelled() const;
    std::vector<bool> enabled() const { return enabled_; }
    Processing processing() const { return processing_; }

    std::shared_ptr<const DecompositionData> data() const;
    std::shared_ptr<const Reconstruction> recon() const;
    std::optional<std::string> error() const;
    std::optional<std::uint64_t> job() const;
    std::shared_ptr<const SignalData> source() const { return source_->data(); }
    std::optional<audio::Clip> clip(ClipKind kind, std::size_t band = 0) const;

    void request_packets(JobRunner& jobs);
    std::shared_ptr<const PacketData> packets() const;
    std::optional<std::string> packets_error() const;
    std::vector<core::PacketNode> basis() const;
    bool set_basis(std::vector<core::PacketNode> b, JobRunner& jobs);
    void make_leaf(core::PacketNode n, JobRunner& jobs);
    double basis_cost() const;
    std::optional<double> basis_pr_error() const;

    void request_map(MapKind kind, JobRunner& jobs);
    std::shared_ptr<const ScalogramData> map(MapKind kind) const;
    std::optional<std::string> map_error(MapKind kind) const;

private:
    void compute(JobRunner& jobs);
    void reconstruct(JobRunner& jobs);
    void remember();

    std::shared_ptr<SignalModel> source_;
    std::size_t budget_;
    DecompositionOptions options_;
    std::vector<bool> enabled_;
    Processing processing_;
    std::optional<std::pair<std::vector<bool>, Processing>> previous_;
    std::shared_ptr<const SignalData> seen_;
    mutable std::mutex mutex_;
    std::shared_ptr<const DecompositionData> data_;
    std::shared_ptr<const Reconstruction> recon_;
    std::optional<std::string> error_;
    std::optional<std::uint64_t> data_job_, recon_job_;
    std::uint64_t data_gen_ = 0, recon_gen_ = 0, data_done_ = 0, recon_done_ = 0;
    bool recon_pending_ = false, cancelled_ = false;
    std::shared_ptr<const PacketData> packets_;
    std::vector<core::PacketNode> basis_;
    std::optional<double> basis_pr_;
    std::uint64_t basis_gen_ = 0, basis_done_ = 0;
    std::optional<std::uint64_t> basis_job_;
    std::optional<std::string> packets_error_;
    std::weak_ptr<const DecompositionData> packets_for_;
    std::optional<std::uint64_t> packets_job_;
    std::uint64_t packets_gen_ = 0, packets_done_ = 0;
    bool packets_cancelled_ = false;
    struct MapSlot {
        std::shared_ptr<const ScalogramData> data;
        std::optional<std::string> error;
        std::weak_ptr<const SignalData> made_for;
        int channel = -1;
        std::optional<std::uint64_t> job;
        std::uint64_t gen = 0, done = 0;
        bool cancelled = false;
    };
    std::array<MapSlot, 2> maps_;
    MapSlot& slot(MapKind kind) { return maps_[static_cast<std::size_t>(kind)]; }
    const MapSlot& slot(MapKind kind) const { return maps_[static_cast<std::size_t>(kind)]; }
};

}  // namespace signals::tui
