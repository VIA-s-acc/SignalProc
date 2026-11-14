#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "io/npy.hpp"
#include "support/paths.hpp"

namespace {

using namespace signals::io;

std::filesystem::path npy_fixture(const std::string& name) {
    return std::filesystem::path{SIGNALS_SOURCE_DIR} / "tests" / "fixtures" / "npy" / name;
}

const nlohmann::json& expected() {
    static const nlohmann::json j = [] {
        std::ifstream in{npy_fixture("expected.json")};
        return nlohmann::json::parse(in);
    }();
    return j;
}

std::string read_bytes(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path& p, std::string_view bytes) {
    std::ofstream out{p, std::ios::binary};
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void check_matches(const NpArray& a, const nlohmann::json& e) {
    REQUIRE(a.shape == e["shape"].get<std::vector<std::size_t>>());
    if (!e["data"].empty() && e["data"][0].is_string()) {
        REQUIRE(std::holds_alternative<std::vector<std::string>>(a.data));
        CHECK(std::get<std::vector<std::string>>(a.data) == e["data"].get<std::vector<std::string>>());
    } else {
        REQUIRE(std::holds_alternative<std::vector<double>>(a.data));
        CHECK(std::get<std::vector<double>>(a.data) == e["data"].get<std::vector<double>>());
    }
}

std::vector<std::uint64_t> bits(const NpArray& a) {
    std::vector<std::uint64_t> out;
    for (const double v : std::get<std::vector<double>>(a.data)) out.push_back(std::bit_cast<std::uint64_t>(v));
    return out;
}

std::string header_claiming(std::string_view shape) {
    std::string dict = "{'descr': '<f8', 'fortran_order': False, 'shape': " + std::string{shape} + ", }";
    while ((10 + dict.size() + 1) % 64 != 0) dict += ' ';
    dict += '\n';
    std::string out = "\x93NUMPY";
    out += '\x01';
    out += '\x00';
    out += static_cast<char>(dict.size() & 0xff);
    out += static_cast<char>(dict.size() >> 8);
    return out + dict + std::string(64, '\0');
}

}  // namespace

TEST_CASE("npy round trip keeps shape and every bit", "[npy]") {
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<NpArray> arrays{np_scalar(2.5), np_vector({-0.0, 1e-308, inf, -inf, nan}),
                                      NpArray{{2, 3}, std::vector<double>{1, 2, 3, 4, 5, 6}}};
    for (const auto& a : arrays) {
        const auto bytes = encode_npy(a);
        REQUIRE(bytes);
        CHECK(bytes->starts_with("\x93NUMPY\x01"));
        const auto header = static_cast<std::size_t>(static_cast<unsigned char>((*bytes)[8])) |
                            static_cast<std::size_t>(static_cast<unsigned char>((*bytes)[9])) << 8;
        CHECK((10 + header) % 64 == 0);
        const auto back = decode_npy(*bytes);
        REQUIRE(back);
        CHECK(back->shape == a.shape);
        CHECK(bits(*back) == bits(a));
    }
    for (const auto& a : {np_text("сигнал Ω"), NpArray{{2}, std::vector<std::string>{"ab", "длинное"}}}) {
        const auto bytes = encode_npy(a);
        REQUIRE(bytes);
        const auto back = decode_npy(*bytes);
        REQUIRE(back);
        CHECK(back->shape == a.shape);
        CHECK(std::get<std::vector<std::string>>(back->data) == std::get<std::vector<std::string>>(a.data));
    }
}

TEST_CASE("npy reads what numpy writes", "[npy]") {
    for (const std::string name : {"f8.npy", "f4.npy", "i1.npy", "i2.npy", "i4.npy", "i8.npy", "u1.npy", "u2.npy", "u4.npy", "u8.npy", "b1.npy",
                                   "be_f8.npy", "fortran.npy", "v2.npy", "v3.npy", "unicode.npy"}) {
        INFO(name);
        const auto a = read_npy(npy_fixture(name));
        INFO((a ? std::string{} : a.error().message));
        REQUIRE(a);
        check_matches(*a, expected()[name]);
    }
}

TEST_CASE("npy refuses complex and object arrays", "[npy]") {
    const auto c = read_npy(npy_fixture("complex.npy"));
    REQUIRE_FALSE(c);
    CHECK(c.error().message.find("c16") != std::string::npos);
    const auto o = read_npy(npy_fixture("object.npy"));
    REQUIRE_FALSE(o);
    CHECK(o.error().message.find("object") != std::string::npos);
}

TEST_CASE("npy checks the size before allocating", "[npy]") {
    const auto wide = decode_npy(header_claiming("(1, 1099511627776)"));
    REQUIRE_FALSE(wide);
    CHECK(wide.error().message.find("268435456") != std::string::npos);
    const auto tall = decode_npy(header_claiming("(5000, 2)"));
    REQUIRE_FALSE(tall);
    CHECK(tall.error().message.find("4096") != std::string::npos);
}

TEST_CASE("npz reads savez and savez_compressed", "[npy]") {
    for (const std::string name : {"savez.npz", "savez_compressed.npz"}) {
        INFO(name);
        const auto entries = read_npz(npy_fixture(name));
        INFO((entries ? std::string{} : entries.error().message));
        REQUIRE(entries);
        REQUIRE(entries->size() == 3);
        CHECK((*entries)[0].first == "a");
        CHECK((*entries)[1].first == "b");
        CHECK((*entries)[2].first == "name");
        for (const auto& [key, a] : *entries) check_matches(a, expected()["npz." + key]);
    }
}

TEST_CASE("npz written by Signals is a stored zip of npy members", "[npy]") {
    const auto path = signals::test::temp_path("stored.npz");
    const NpzEntries entries{{"x", NpArray{{2, 3}, std::vector<double>{1, 2, 3, 4, 5, 6}}}, {"rate", np_scalar(8000)}, {"name", np_text("db2")}};
    REQUIRE(write_npz(path, entries));
    const auto raw = read_bytes(path);
    REQUIRE(raw.starts_with("PK\x03\x04"));
    CHECK(raw[8] == 0);
    CHECK(raw[9] == 0);
    CHECK(raw.find("x.npy") != std::string::npos);
    CHECK(raw.find("rate.npy") != std::string::npos);
    const auto back = read_npz(path);
    REQUIRE(back);
    REQUIRE(back->size() == 3);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK((*back)[i].first == entries[i].first);
        CHECK((*back)[i].second.shape == entries[i].second.shape);
        CHECK((*back)[i].second.data == entries[i].second.data);
    }
    CHECK(np_find(*back, "rate"));
    CHECK_FALSE(np_find(*back, "missing"));
}

TEST_CASE("npz works under a Cyrillic folder with spaces", "[npy]") {
    const auto dir = signals::test::temp_path("npz-unicode") / std::filesystem::path{u8"данные с пробелом"};
    std::filesystem::create_directories(dir);
    const auto npz = dir / std::filesystem::path{u8"сигнал.npz"};
    REQUIRE(write_npz(npz, {{"x", np_vector({0.25, -0.5})}}));
    const auto back = read_npz(npz);
    REQUIRE(back);
    CHECK(std::get<std::vector<double>>((*back)[0].second.data) == std::vector<double>{0.25, -0.5});
    const auto npy = dir / std::filesystem::path{u8"сигнал.npy"};
    REQUIRE(write_npy(npy, np_vector({1.5})));
    const auto one = read_npy(npy);
    REQUIRE(one);
    CHECK(std::get<std::vector<double>>(one->data) == std::vector<double>{1.5});
}

TEST_CASE("broken npy and npz give a readable error", "[npy]") {
    const auto truncated = signals::test::temp_path("truncated.npy");
    const auto whole = read_bytes(npy_fixture("f8.npy"));
    write_bytes(truncated, whole.substr(0, whole.size() - 5));
    const auto t = read_npy(truncated);
    REQUIRE_FALSE(t);
    CHECK(t.error().message.find("truncated.npy") != std::string::npos);

    const auto magic = signals::test::temp_path("magic.npy");
    write_bytes(magic, "not an array at all");
    const auto m = read_npy(magic);
    REQUIRE_FALSE(m);
    CHECK(m.error().message.find("magic.npy") != std::string::npos);

    const auto member = read_npz(npy_fixture("bad_member.npz"));
    REQUIRE_FALSE(member);
    CHECK(member.error().message.find("readme.txt") != std::string::npos);

    const auto cut = signals::test::temp_path("cut.npz");
    write_bytes(cut, read_bytes(npy_fixture("savez.npz")).substr(0, 100));
    const auto c = read_npz(cut);
    REQUIRE_FALSE(c);
    CHECK(c.error().message.find("cut.npz") != std::string::npos);
}
