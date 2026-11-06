#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include "core/bank.hpp"
#include "core/error.hpp"
#include "core/freq.hpp"

namespace signals::tui {

enum class ExplainBank { haar, db2, legall53, user };

struct ExplainOptions {
    ExplainBank bank = ExplainBank::haar;
    bool odd_phase = true;
    bool summing = false;
    bool zero_detail = false;
    bool perturb_f1 = false;
    std::optional<core::FilterBank> user = std::nullopt;
};

struct Walkthrough {
    core::FilterBank bank;
    std::string bank_name;
    ExplainOptions options;
    std::vector<double> x, y0, y1, a, d, u0, u1, w0, w1, xhat;
    std::vector<std::size_t> kept;
    std::size_t window = 0;
    std::ptrdiff_t delay = 0;
    double energy_x = 0, energy_a = 0, energy_d = 0;
    std::vector<std::vector<double>> tree;
    Eigen::MatrixXd a1;
    bool orthogonal = false;
    core::FrequencyResponse h0_response, h1_response;
    double max_alias_db = 0;
    double branch_alias_db = 0;
    double max_error = 0;
};

core::Result<Walkthrough> walkthrough(std::span<const double> x, const ExplainOptions& options);

struct ExplainStep {
    std::string title;
    std::vector<std::string> highlight;
    std::vector<std::string> rows;
    std::vector<std::string> text, text_haar;
    std::string invariant;
    std::string view;
    bool mark_kept = false;
};

core::Result<std::vector<ExplainStep>> load_script(std::string_view toml);
const std::vector<ExplainStep>& haar_script();
core::Result<std::string> fill(std::string_view line, const Walkthrough& w);

struct Invariant {
    double value = 0;
    double tolerance = 0;
    bool passed = false;
};
Invariant check_invariant(std::string_view id, const Walkthrough& w);

std::string format_cell(double v, bool fractions);

}  // namespace signals::tui
