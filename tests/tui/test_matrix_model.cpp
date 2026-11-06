#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

#include "core/conv.hpp"
#include "core/design/families.hpp"
#include "core/dwt.hpp"
#include "core/wavedec.hpp"
#include "tui/screens/matrix_model.hpp"

using namespace signals::tui;
using namespace std::chrono_literals;
namespace core = signals::core;

namespace {

struct Rig {
    JobRunner jobs{[] {}, 0ms, true};
    MatrixModel m;
};

}  // namespace

TEST_CASE("db2 periodization N=32 is orthogonal and A x matches dwt_level", "[tui][matrix]") {
    Rig r;
    r.m.set({}, r.jobs);
    auto d = r.m.data();
    REQUIRE(d);
    CHECK(d->m.rows() == 32);
    CHECK(d->square);
    REQUIRE(d->orth_error);
    CHECK(*d->orth_error < 1e-14);
    CHECK(d->cond == Catch::Approx(1).margin(1e-12));
    CHECK(d->x[5] == std::round(8 * std::sin(2 * M_PI * 5 / 16) + 2.5));
    auto lvl = core::dwt_level<double>(d->x, *core::wavelet("db2"), {core::Extension::periodization});
    REQUIRE(lvl);
    CHECK(d->y[5] == Catch::Approx(lvl->approx[5]));
    CHECK(d->y[16 + 3] == Catch::Approx(lvl->detail[3]));
    CHECK(d->outputs[5] == "a₅");
    CHECK(d->row_names[16] == "B₀");
    CHECK(d->detail_start == 16);
    const auto t = r.m.row_terms(5);
    CHECK(t.size() == 4);
    double sum = 0;
    for (const auto& term : t) sum += term.product;
    CHECK(sum == Catch::Approx(d->y[5]));
    REQUIRE(d->transpose_error);
    CHECK(*d->transpose_error < 1e-14);
    CHECK(d->toeplitz.rows() == 12);
}

TEST_CASE("symmetric db2 is not square and says why", "[tui][matrix]") {
    Rig r;
    r.m.set({.mode = core::Extension::symmetric}, r.jobs);
    CHECK_FALSE(r.m.data()->square);
    CHECK(r.m.data()->m.rows() > 32);
}

TEST_CASE("interleaved order permutes rows", "[tui][matrix]") {
    Rig r;
    r.m.set({}, r.jobs);
    const Eigen::MatrixXd blocks = r.m.data()->m;
    r.m.set({.order = RowOrder::interleaved}, r.jobs);
    const auto& inter = r.m.data()->m;
    CHECK((inter.row(1) - blocks.row(16)).cwiseAbs().maxCoeff() == 0.0);
    CHECK((inter.row(2) - blocks.row(1)).cwiseAbs().maxCoeff() == 0.0);
    CHECK(r.m.data()->row_names[1] == "B₀");
}

TEST_CASE("multilevel W equals wavedec with periodization", "[tui][matrix]") {
    Rig r;
    r.m.set({.op = MatrixOp::multilevel, .n = 16, .levels = 2}, r.jobs);
    const auto d = r.m.data();
    REQUIRE(d);
    auto c = core::wavedec<double>(d->x, *core::wavelet("db2"), 2, {core::Extension::periodization});
    REQUIRE(c);
    std::vector<double> flat;
    for (const auto& b : c->bands) flat.insert(flat.end(), b.begin(), b.end());
    REQUIRE(flat.size() == d->y.size());
    for (std::size_t i = 0; i < flat.size(); ++i) CHECK(d->y[i] == Catch::Approx(flat[i]).margin(1e-12));
    CHECK(d->outputs.front() == "a₂[0]");
}

TEST_CASE("circulant times x is circular convolution", "[tui][matrix]") {
    Rig r;
    r.m.set({.op = MatrixOp::circulant, .n = 16}, r.jobs);
    const auto d = r.m.data();
    const auto expected = core::circular_convolve(d->x, core::wavelet("db2")->h0);
    for (std::size_t i = 0; i < expected.size(); ++i) CHECK(d->y[i] == Catch::Approx(expected[i]).margin(1e-12));
}

TEST_CASE("S A is the identity for bior2.2 periodization", "[tui][matrix]") {
    Rig r;
    r.m.set({.wavelet = "bior2.2", .op = MatrixOp::synthesis_product, .n = 16}, r.jobs);
    const auto d = r.m.data();
    REQUIRE(d->left_inverse_error);
    CHECK(*d->left_inverse_error < 1e-12);
    CHECK((d->m - Eigen::MatrixXd::Identity(16, 16)).cwiseAbs().maxCoeff() < 1e-12);
}

TEST_CASE("the Gram toggle shows AᵀA", "[tui][matrix]") {
    Rig r;
    r.m.set({.gram = true}, r.jobs);
    CHECK((r.m.data()->m - Eigen::MatrixXd::Identity(32, 32)).cwiseAbs().maxCoeff() < 1e-14);
}

TEST_CASE("a signal shorter than the filter says which N is needed", "[tui][matrix]") {
    Rig r;
    r.m.set({.wavelet = "db10", .op = MatrixOp::multilevel, .n = 8}, r.jobs);
    const auto d = r.m.data();
    REQUIRE(d);
    REQUIRE(d->error);
    CHECK(d->m.size() == 0);
    CHECK(d->error->find("N ≥ 38") != std::string::npos);
    CHECK(d->error->find("wavedec") == std::string::npos);
}

TEST_CASE("levels the signal cannot hold are lowered with a note", "[tui][matrix]") {
    Rig r;
    r.m.set({.wavelet = "bior3.7", .op = MatrixOp::multilevel, .n = 32, .levels = 3}, r.jobs);
    auto d = r.m.data();
    REQUIRE(d);
    CHECK_FALSE(d->error);
    CHECK(d->levels == 1);
    CHECK(d->m.rows() == 32);
    CHECK(d->row_names.back() == "d₁[15]");
    REQUIRE(d->note);
    CHECK(d->note->find("J = 1") != std::string::npos);
    CHECK(d->note->find("N ≥ 120") != std::string::npos);
    CHECK_FALSE(r.m.job());
    r.m.set({.wavelet = "bior3.7", .op = MatrixOp::multilevel, .n = 128, .levels = 3}, r.jobs);
    d = r.m.data();
    REQUIRE(d);
    CHECK(d->levels == 3);
    CHECK_FALSE(d->note);
}

TEST_CASE("odd and out-of-range N are clamped", "[tui][matrix]") {
    Rig r;
    r.m.set({.n = 33}, r.jobs);
    CHECK(r.m.options().n == 32);
    r.m.set({.n = 1}, r.jobs);
    CHECK(r.m.options().n == 2);
    r.m.set({.n = 1000}, r.jobs);
    CHECK(r.m.options().n == 256);
}
