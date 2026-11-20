#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>
#include <vector>

#include "core/progress.hpp"
#include "io/npy.hpp"
#include "pipeline/cache.hpp"
#include "pipeline/executor.hpp"
#include "pipeline/graph.hpp"
#include "pipeline/migrate.hpp"
#include "pipeline/npz_values.hpp"

using namespace signals;

namespace {

std::string bytes(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

bool same_bits(const io::NpArray& a, const io::NpArray& b) {
    if (a.shape != b.shape || a.data.index() != b.data.index()) return false;
    if (const auto* x = std::get_if<std::vector<double>>(&a.data)) {
        const auto& y = std::get<std::vector<double>>(b.data);
        return std::ranges::equal(*x, y, [](double p, double q) { return std::bit_cast<std::uint64_t>(p) == std::bit_cast<std::uint64_t>(q); });
    }
    return a.data == b.data;
}

bool close(const io::NpArray& a, const io::NpArray& b) {
    const auto* x = std::get_if<std::vector<double>>(&a.data);
    const auto* y = std::get_if<std::vector<double>>(&b.data);
    if (!x || !y || a.shape != b.shape) return same_bits(a, b);
    return std::ranges::equal(*x, *y, [](double p, double q) { return std::abs(p - q) <= 1e-9 * std::max(1.0, std::abs(q)); });
}

io::NpzEntries probes_of(const pipeline::PipelineDoc& doc, const std::filesystem::path& dir) {
    const auto graph = pipeline::build_graph(doc, dir);
    REQUIRE(graph);
    pipeline::ResultCache cache{0};
    core::CancelToken cancel;
    const auto result = pipeline::run_graph(*graph, cache, {}, cancel);
    REQUIRE(result);
    io::NpzEntries out;
    for (const auto& [name, value] : result->probes) {
        auto entries = pipeline::value_entries(value);
        REQUIRE(entries);
        for (auto& e : pipeline::prefixed(name, std::move(*entries))) out.push_back(std::move(e));
    }
    return out;
}

std::vector<std::filesystem::path> samples() {
    std::vector<std::filesystem::path> out;
    for (const auto& e : std::filesystem::directory_iterator{std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "sig" / "v1"})
        if (e.path().extension() == ".sig" && !e.path().filename().string().ends_with(".v2.sig")) out.push_back(e.path());
    std::ranges::sort(out);
    return out;
}

}  // namespace

TEST_CASE("every v1 sample migrates to its expected text and numbers", "[v1-corpus]") {
    // The migrated v1 file must equal the expected v2 file bit for bit on this build; the numbers 0.9.0 wrote on macOS
    // are matched to 1e-9 because libm differs in the last bits between platforms (atan2 and exp in the phase vocoder).
    const auto list = samples();
    REQUIRE(list.size() >= 14);
    for (const auto& file : list) {
        INFO(file.filename().string());
        const auto loaded = pipeline::load_sig(bytes(file));
        REQUIRE(loaded);
        CHECK(loaded->from_version == 1);
        auto expected = file;
        expected.replace_extension(".v2.sig");
        CHECK(pipeline::write_sig(loaded->doc) == bytes(expected));
        const auto got = probes_of(loaded->doc, file.parent_path());
        const auto current = pipeline::load_sig(bytes(expected));
        REQUIRE(current);
        const auto same_platform = probes_of(current->doc, file.parent_path());
        REQUIRE(got.size() == same_platform.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            INFO(got[i].first);
            CHECK(got[i].first == same_platform[i].first);
            CHECK(same_bits(got[i].second, same_platform[i].second));
        }
        auto recorded = file;
        recorded.replace_extension(".npz");
        const auto want = io::read_npz(recorded);
        REQUIRE(want);
        CHECK(got.size() == want->size());
        for (const auto& [key, array] : *want) {
            INFO(key);
            const auto* mine = io::np_find(got, key);
            REQUIRE(mine);
            CHECK(close(*mine, array));
        }
    }
}
