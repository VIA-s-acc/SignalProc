#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include "core/extension.hpp"
#include "tui/jobs/jobs.hpp"

namespace signals::tui {

enum class MatrixOp { analysis, multilevel, convolution, circulant, synthesis_product };
enum class RowOrder { blocks, interleaved };

struct MatrixOptions {
    std::string wavelet = "db2";
    MatrixOp op = MatrixOp::analysis;
    core::Extension mode = core::Extension::periodization;
    RowOrder order = RowOrder::blocks;
    std::size_t n = 32;
    std::size_t levels = 3;
    bool gram = false;
};

struct RowTerm {
    double coef;
    std::size_t column;
    double x;
    double product;
};

struct MatrixData {
    MatrixOptions options;
    Eigen::MatrixXd m;
    std::vector<std::string> row_names;
    std::vector<std::string> outputs;
    std::size_t detail_start = 0;
    std::vector<double> x, y;
    std::optional<double> orth_error, transpose_error, left_inverse_error;
    std::vector<double> singular;
    double cond = 0;
    bool square = true;
    bool orthogonal_bank = false;
    std::size_t filter_length = 0;
    std::optional<std::string> error;
    Eigen::MatrixXd toeplitz;
};

class MatrixModel {
public:
    void set(MatrixOptions o, JobRunner& jobs);
    MatrixOptions options() const { return options_; }
    std::shared_ptr<const MatrixData> data() const;
    std::vector<RowTerm> row_terms(std::size_t row) const;
    std::optional<std::uint64_t> job() const;

private:
    MatrixOptions options_;
    mutable std::mutex mutex_;
    std::shared_ptr<const MatrixData> data_;
    std::optional<std::uint64_t> job_;
};

}  // namespace signals::tui
