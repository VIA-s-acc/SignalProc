#include <bit>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "pipeline/block.hpp"
#include "python/form.hpp"

namespace {

using namespace signals;
using namespace signals::python;

bool same_bits(double a, double b) { return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b); }

}  // namespace

TEST_CASE("python names avoid keywords, builtins and imports", "[py-forms]") {
    const std::set<std::string, std::less<>> none;
    CHECK(py_name("lambda", none) == "lambda_");
    CHECK(py_name("print", none) == "print_");
    CHECK(py_name("np", none) == "np_");
    CHECK(py_name("pywt", none) == "pywt_");
    CHECK(py_name("D", none) == "D_");
    CHECK(py_name("verify", none) == "verify_");
    CHECK(py_name("signal", none) == "signal_");
    CHECK(py_name("noisy", none) == "noisy");
    const std::set<std::string, std::less<>> taken{"x"};
    CHECK(py_name("x", taken) == "x_");
    CHECK(py_name("X", taken) == "X");
    Emitter em;
    CHECK(em.name_for("x") == "x");
    CHECK(em.name_for("x") == "x_");
}

TEST_CASE("numbers round-trip through their python text", "[py-forms]") {
    const Emitter em;
    for (const double v : {0.1, 1e-300, -0.0, 2.0 / 3, 1e22, 8000.0, 5e-324}) {
        INFO(v);
        const auto text = em.number(v);
        CHECK(same_bits(std::strtod(text.c_str(), nullptr), v));
    }
    CHECK(em.number(std::numeric_limits<double>::infinity()) == "np.inf");
    CHECK(em.number(-std::numeric_limits<double>::infinity()) == "-np.inf");
    CHECK(em.number(std::numeric_limits<double>::quiet_NaN()) == "np.nan");
}

TEST_CASE("long parameter lists go to the data file", "[py-forms]") {
    Emitter em;
    em.set_node("fir1");
    const std::vector<double> short_list(64, 0.5), long_list(65, 0.25);
    CHECK(em.numbers("taps", short_list).starts_with("[0.5, 0.5"));
    CHECK(em.entries().empty());
    CHECK(em.numbers("taps", long_list) == "D[\"fir1.taps\"]");
    REQUIRE(io::np_find(em.entries(), "fir1.taps"));
    CHECK(io::np_count(*io::np_find(em.entries(), "fir1.taps")) == 65);
}

TEST_CASE("wavelets go by name only when pywt has the same filters", "[py-forms]") {
    Emitter em;
    CHECK(em.wavelet("db2") == "\"db2\"");
    const auto w = em.wavelet("sym8");
    CHECK(w == em.wavelet("sym8"));
    REQUIRE(em.prelude().size() == 1);
    CHECK(em.prelude().front().find(w + " = pywt.Wavelet(\"sym8\", filter_bank=") == 0);
    for (const std::string key : {"wavelet.sym8.dec_lo", "wavelet.sym8.dec_hi", "wavelet.sym8.rec_lo", "wavelet.sym8.rec_hi"}) CHECK(io::np_find(em.entries(), key));
    CHECK(std::find(em.imports().begin(), em.imports().end(), "import pywt") != em.imports().end());
    for (const auto& name : by_filter_bank()) CHECK(name.starts_with("sym"));
}

TEST_CASE("every python function file defines its own name", "[py-forms]") {
    const auto files = python_functions();
    REQUIRE_FALSE(files.empty());
    for (const auto& f : files) {
        INFO(f.name);
        CHECK(f.text.find("def " + std::string{f.name} + "(") != std::string::npos);
        for (const auto& need : function_needs(f.text)) CHECK(find_python_function(need));
    }
    Emitter em;
    em.function("verify");
    em.function("no_such_function");
    REQUIRE(em.functions().size() == 1);
    CHECK(em.functions().front().find("def verify(") != std::string::npos);
    CHECK(em.missing() == std::vector<std::string>{"no_such_function"});
}

TEST_CASE("unsupported names the node, block and parameter", "[py-forms]") {
    pipeline::GraphNode node;
    node.id = "boost";
    node.type = pipeline::find_block_type("gain");
    REQUIRE(node.type);
    node.params = {{"db", "6"}};
    const NodeIo io{node, {"x"}, {"boost"}, {"8000"}, {}, {}};
    const auto e = unsupported(io, "db");
    CHECK(e.code == core::Errc::unsupported);
    for (const char* part : {"boost", "gain", "db", "6"}) CHECK(e.message.find(part) != std::string::npos);
}
