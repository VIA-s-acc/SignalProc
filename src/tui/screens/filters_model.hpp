#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/freq.hpp"
#include "core/laurent.hpp"
#include "tui/jobs/jobs.hpp"

namespace signals::tui {

enum class FilterRole { h0, h1, f0, f1 };

struct FilterRef {
    enum class Kind { bank, equivalent, distortion, alias, user };
    Kind kind = Kind::bank;
    std::string wavelet;
    FilterRole role = FilterRole::h0;
    std::size_t level = 0;
    bool detail = false;

    std::string label() const;
    friend bool operator==(const FilterRef&, const FilterRef&) = default;
};

struct LibraryNode {
    std::string label;
    std::vector<LibraryNode> children = {};
    std::optional<FilterRef> ref = {};
    std::string later = {};
};

LibraryNode build_library(std::string_view equivalent_wavelet = "db2", std::size_t levels = 3);

struct FilterAnalysis {
    FilterRef ref;
    core::Laurent h;
    core::FrequencyResponse response;
    std::optional<core::ZerosPoles> zp;
    core::BandMetrics metrics;
    std::optional<int> zeros_at_minus_one;
    std::optional<double> pr_error, orth_error, sobolev;
    bool linear_phase = false;
};

core::Result<FilterAnalysis> analyze(const FilterRef& ref, std::size_t points);

core::FrequencyResponse range_response(const core::Laurent& h, double lo, double hi, std::size_t points,
                                       const core::FrequencyResponse* full = nullptr);
std::vector<double> masked_group_delay(const core::FrequencyResponse& r, double relative = 1e-3, std::optional<double> peak = std::nullopt);

class ComparisonSet {
public:
    static constexpr std::size_t kCapacity = 5;

    struct Entry {
        FilterRef ref;
        bool visible = true;
        std::shared_ptr<const FilterAnalysis> analysis = {};
        std::optional<std::string> error = {};
        std::shared_ptr<const core::FrequencyResponse> view = {};
        double view_lo = 0, view_hi = 1;
    };

    core::Result<void> add(FilterRef ref);
    void remove(std::size_t i);
    void toggle(std::size_t i);
    void set_reference(std::optional<std::size_t> i);
    void set_focus(std::size_t i);

    std::vector<Entry> entries() const;
    std::optional<std::size_t> reference() const;
    std::size_t focus() const;

    void request(JobRunner& jobs, std::size_t points);
    void request_view(JobRunner& jobs, double lo, double hi, std::size_t points);
    std::optional<std::string> pending_label() const;
    bool view_pending() const;

private:
    void store(const FilterRef& ref, std::shared_ptr<const FilterAnalysis> analysis, std::optional<std::string> error);

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::optional<std::size_t> reference_;
    std::size_t focus_ = 0;
    std::vector<FilterRef> pending_;
    bool view_pending_ = false;
};

}  // namespace signals::tui
