#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/bank.hpp"
#include "core/design/split.hpp"
#include "core/freq.hpp"
#include "core/wavelet/cascade.hpp"
#include "tui/jobs/jobs.hpp"

namespace signals::tui {

enum class Preset { daubechies, max_phase, cdf, degenerate };

struct SplitRow {
    core::Split split;
    std::size_t h0_len = 0, f0_len = 0;
    core::SplitInfo info;
    bool linear_phase = false;
    bool orthogonal = false;
};

struct DesignerData {
    int p = 0;
    core::HalfbandFactorisation f;
    std::vector<std::complex<double>> roots;
    std::vector<SplitRow> rows;
    std::size_t wavelet_rows = 0;
};

struct DesignerResult {
    core::Split split;
    core::FilterBank bank;
    core::PrReport pr;
    std::optional<std::ptrdiff_t> delay;
    bool orthogonal = false, linear_phase = false;
    core::SplitInfo info;
    std::optional<core::Cascade> phi, phi_dual;
    std::vector<core::Cascade> phi_steps, phi_dual_steps;
    core::FrequencyResponse h0_response, f0_response;
    std::string title;
};

struct Constraints {
    bool linear_phase = false, orthogonal = false, wavelet_only = false;
};

core::Split preset_split(const core::HalfbandFactorisation& f, Preset preset);
bool same_split(const core::Split& a, const core::Split& b);

class DesignerModel {
public:
    static constexpr int kMinP = 1, kMaxP = 8, kMinIterations = 2, kMaxIterations = 8;

    void set_p(int p, JobRunner& jobs);
    int p() const { return p_; }
    void sync(JobRunner& jobs);
    std::shared_ptr<const DesignerData> data() const;
    const core::Split& split() const { return split_; }
    void set_split(core::Split s, JobRunner& jobs);
    void toggle_group(std::size_t g, JobRunner& jobs);
    void zeros(int delta, JobRunner& jobs);
    void swap(JobRunner& jobs);
    void preset(Preset p, JobRunner& jobs);
    Constraints constraints() const { return constraints_; }
    void set_constraints(Constraints c, JobRunner& jobs);
    std::vector<std::size_t> visible_rows() const;
    void step(int direction, JobRunner& jobs);
    int cascade_iterations() const { return iterations_; }
    void set_cascade_iterations(int i);
    std::shared_ptr<const DesignerResult> result() const;
    std::optional<std::uint64_t> job() const;

private:
    bool allowed(const SplitRow& row, const DesignerData& d) const;
    void request(JobRunner& jobs);

    mutable std::mutex mutex_;
    std::map<int, std::shared_ptr<const DesignerData>> cache_;
    std::shared_ptr<const DesignerResult> result_;
    std::optional<std::uint64_t> data_job_, result_job_;
    int p_ = 3;
    core::Split split_;
    Constraints constraints_;
    int iterations_ = kMaxIterations;
};

}  // namespace signals::tui
