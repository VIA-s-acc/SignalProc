#include "tui/screens/matrix_model.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/bank.hpp"
#include "core/design/families.hpp"
#include "core/matrix.hpp"
#include "core/wavedec.hpp"
#include "i18n/i18n.hpp"
#include "tui/widgets/text_util.hpp"

namespace signals::tui {
namespace {

constexpr std::size_t kMinN = 2, kMaxN = 256, kToeplitz = 12;

std::string sub(std::size_t k) { return subscript_count(k); }

std::vector<double> test_signal(std::size_t n) {
    std::vector<double> x(n);
    for (std::size_t k = 0; k < n; ++k) x[k] = std::round(8 * std::sin(2 * std::numbers::pi * static_cast<double>(k) / 16) + static_cast<double>(k) / 2);
    return x;
}

}  // namespace

std::shared_ptr<const MatrixData> MatrixModel::data() const {
    std::lock_guard lock{mutex_};
    return data_;
}

std::optional<std::uint64_t> MatrixModel::job() const {
    std::lock_guard lock{mutex_};
    return job_;
}

std::vector<RowTerm> MatrixModel::row_terms(std::size_t row) const {
    const auto d = data();
    std::vector<RowTerm> out;
    if (!d || row >= static_cast<std::size_t>(d->m.rows())) return out;
    for (Eigen::Index c = 0; c < d->m.cols(); ++c) {
        const double coef = d->m(static_cast<Eigen::Index>(row), c);
        if (std::abs(coef) < 1e-12) continue;
        const auto col = static_cast<std::size_t>(c);
        const double x = col < d->x.size() ? d->x[col] : 0.0;
        out.push_back({coef, col, x, coef * x});
    }
    return out;
}

void MatrixModel::set(MatrixOptions o, JobRunner& jobs) {
    o.n = std::clamp(o.n - o.n % 2, kMinN, kMaxN);
    o.levels = std::max<std::size_t>(1, o.levels);
    options_ = o;
    {
        std::lock_guard lock{mutex_};
        if (job_) jobs.cancel(*job_);
    }
    const auto id = jobs.submit(tr("tui.matrix.job"), {tr("tui.matrix.stage")}, [this, o](JobContext& ctx) {
        ctx.begin(0);
        auto d = std::make_shared<MatrixData>();
        d->options = o;
        auto fail = [&](std::string message) {
            d->error = std::move(message);
            ctx.finish();
            std::lock_guard lock{mutex_};
            data_ = std::move(d);
            job_.reset();
        };
        const auto bank = core::wavelet(o.wavelet);
        if (!bank) return fail(bank.error().message);
        d->filter_length = core::trim(bank->h0, 0.0).coeffs.size();
        d->orthogonal_bank = core::check_orthogonality(*bank, 1e-9).passed();
        const core::DwtOptions dwt{o.mode, false};
        Eigen::MatrixXd a;
        switch (o.op) {
        case MatrixOp::analysis:
        case MatrixOp::synthesis_product: {
            auto am = core::analysis_matrix(*bank, o.n, dwt);
            if (!am) return fail(am.error().message);
            a = std::move(*am);
            const auto len = static_cast<std::size_t>(a.rows() / 2);
            d->detail_start = len;
            for (std::size_t k = 0; k < len; ++k) {
                d->row_names.push_back("L" + sub(k));
                d->outputs.push_back("a" + sub(k));
            }
            for (std::size_t k = 0; k < len; ++k) {
                d->row_names.push_back("B" + sub(k));
                d->outputs.push_back("d" + sub(k));
            }
            if (auto s = core::synthesis_matrix(*bank, o.n, dwt); s) {
                if (s->rows() == a.cols() && s->cols() == a.rows()) d->transpose_error = (*s - a.transpose()).cwiseAbs().maxCoeff();
                const auto x = test_signal(o.n);
                const Eigen::Map<const Eigen::VectorXd> xv(x.data(), static_cast<Eigen::Index>(x.size()));
                if (s->cols() == a.rows()) d->left_inverse_error = ((*s) * (a * xv) - xv).cwiseAbs().maxCoeff();
                if (o.op == MatrixOp::synthesis_product) {
                    a = (*s) * a;
                    d->row_names.clear();
                    d->outputs.clear();
                    for (std::size_t k = 0; k < o.n; ++k) {
                        d->row_names.push_back("(SA)" + sub(k));
                        d->outputs.push_back("x̂" + sub(k));
                    }
                    d->detail_start = o.n;
                }
            }
            if (o.op == MatrixOp::analysis && o.order == RowOrder::interleaved) {
                Eigen::MatrixXd p(a.rows(), a.cols());
                std::vector<std::string> names, outputs;
                for (std::size_t k = 0; k < len; ++k) {
                    p.row(static_cast<Eigen::Index>(2 * k)) = a.row(static_cast<Eigen::Index>(k));
                    p.row(static_cast<Eigen::Index>(2 * k + 1)) = a.row(static_cast<Eigen::Index>(len + k));
                    names.push_back(d->row_names[k]);
                    names.push_back(d->row_names[len + k]);
                    outputs.push_back(d->outputs[k]);
                    outputs.push_back(d->outputs[len + k]);
                }
                a = std::move(p);
                d->row_names = std::move(names);
                d->outputs = std::move(outputs);
                d->detail_start = 1;
            }
            break;
        }
        case MatrixOp::multilevel: {
            auto w = core::multilevel_matrix(*bank, o.n, o.levels, dwt);
            if (!w) return fail(w.error().message);
            a = std::move(*w);
            const auto probe = core::wavedec<double>(std::vector<double>(o.n, 0.0), *bank, o.levels, dwt);
            if (probe) {
                for (std::size_t b = 0; b < probe->bands.size(); ++b) {
                    const auto band = (b == 0 ? "a" : "d") + sub(b == 0 ? o.levels : o.levels + 1 - b);
                    if (b == 1) d->detail_start = d->row_names.size();
                    for (std::size_t k = 0; k < probe->bands[b].size(); ++k) {
                        d->row_names.push_back(band + "[" + std::to_string(k) + "]");
                        d->outputs.push_back(band + "[" + std::to_string(k) + "]");
                    }
                }
            }
            break;
        }
        case MatrixOp::convolution:
        case MatrixOp::circulant:
            a = o.op == MatrixOp::convolution ? core::convolution_matrix(bank->h0, o.n) : core::circulant_matrix(bank->h0, o.n);
            for (Eigen::Index k = 0; k < a.rows(); ++k) {
                d->row_names.push_back("y" + sub(static_cast<std::size_t>(k)));
                d->outputs.push_back("y" + sub(static_cast<std::size_t>(k)));
            }
            d->detail_start = static_cast<std::size_t>(a.rows());
            break;
        }
        if (o.op != MatrixOp::convolution && o.op != MatrixOp::circulant) {
            d->orth_error = core::check_orthogonal_matrix(a, 0).value;
            d->singular = core::singular_values(a);
            d->cond = core::condition_number(d->singular);
        }
        d->x = test_signal(o.n);
        const Eigen::Map<const Eigen::VectorXd> xv(d->x.data(), static_cast<Eigen::Index>(d->x.size()));
        const Eigen::VectorXd y = a * xv;
        d->y.assign(y.data(), y.data() + y.size());
        d->square = a.rows() == a.cols();
        if (o.gram) {
            a = a.transpose() * a;
            d->row_names.clear();
            d->outputs.clear();
            for (Eigen::Index k = 0; k < a.rows(); ++k) {
                d->row_names.push_back("G" + sub(static_cast<std::size_t>(k)));
                d->outputs.push_back("G" + sub(static_cast<std::size_t>(k)));
            }
            d->detail_start = static_cast<std::size_t>(a.rows());
        }
        d->m = std::move(a);
        const Eigen::MatrixXd t = core::convolution_matrix(bank->h0, kToeplitz);
        d->toeplitz = t.topRows(std::min<Eigen::Index>(t.rows(), static_cast<Eigen::Index>(kToeplitz)));
        ctx.finish();
        std::lock_guard lock{mutex_};
        data_ = std::move(d);
        job_.reset();
    });
    std::lock_guard lock{mutex_};
    if (!data_ || data_->options.n != o.n || data_->options.op != o.op || data_->options.wavelet != o.wavelet || data_->options.mode != o.mode ||
        data_->options.order != o.order || data_->options.gram != o.gram || data_->options.levels != o.levels)
        job_ = id;
}

}  // namespace signals::tui
