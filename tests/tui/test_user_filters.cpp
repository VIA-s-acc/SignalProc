#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "core/design/families.hpp"
#include "tui/data/user_filters.hpp"
#include "tui/screens/filters_model.hpp"

using namespace signals::tui;
namespace core = signals::core;

namespace {

std::filesystem::path fresh_dir(const char* name) {
    auto dir = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(dir);
    return dir;
}

}  // namespace

TEST_CASE("user banks round-trip through .flt files", "[tui][user_filters]") {
    const auto dir = fresh_dir("signals-user-filters-roundtrip");
    UserBank b{"Мой db3", core::wavelet("db3")->h0, core::wavelet("db3")->f0, "p = 3"};
    auto path = save_user_bank(dir, b);
    REQUIRE(path);
    CHECK(path->filename() == "Мой_db3.flt");
    auto lib = load_user_banks(dir);
    REQUIRE(lib.banks.size() == 1);
    CHECK(lib.errors.empty());
    CHECK(lib.banks[0].name == "Мой db3");
    CHECK(lib.banks[0].h0 == b.h0);
    CHECK(lib.banks[0].f0 == b.f0);
    CHECK(lib.banks[0].note == "p = 3");
    auto second = save_user_bank(dir, b);
    REQUIRE(second);
    CHECK(second->filename() == "Мой_db3-2.flt");
}

TEST_CASE("a broken .flt is reported and skipped", "[tui][user_filters]") {
    const auto dir = fresh_dir("signals-user-filters-broken");
    UserBank ok{"ok", core::wavelet("haar")->h0, core::wavelet("haar")->f0, ""};
    REQUIRE(save_user_bank(dir, ok));
    std::ofstream{dir / "bad.flt"} << "h0 = [";
    std::ofstream{dir / "notes.txt"} << "ignored";
    auto lib = load_user_banks(dir);
    CHECK(lib.banks.size() == 1);
    REQUIRE(lib.errors.size() == 1);
    CHECK(lib.errors[0].find("bad.flt") != std::string::npos);
    CHECK(load_user_banks(dir / "missing").banks.empty());
}

TEST_CASE("the user filters directory follows XDG", "[tui][user_filters]") {
    auto env = [](std::string_view k) -> std::optional<std::string> {
        if (k == "XDG_CONFIG_HOME") return "/x";
        if (k == "HOME") return "/h";
        return std::nullopt;
    };
    CHECK(user_filters_dir(env) == std::filesystem::path{"/x/signals/filters"});
    auto home_only = [](std::string_view k) -> std::optional<std::string> { return k == "HOME" ? std::optional<std::string>{"/h"} : std::nullopt; };
    CHECK(user_filters_dir(home_only) == std::filesystem::path{"/h/.config/signals/filters"});
}

TEST_CASE("the filters library lists saved banks and analyses them", "[tui][user_filters][filters]") {
    set_user_banks({{"Мой db3", core::wavelet("db3")->h0, core::wavelet("db3")->f0, ""}});
    const auto lib = build_library();
    const LibraryNode* mine = nullptr;
    for (const auto& g : lib.children)
        if (g.label == "Мои фильтры") mine = &g;
    REQUIRE(mine);
    REQUIRE(mine->children.size() == 1);
    CHECK(mine->children[0].label == "Мой db3");
    REQUIRE(mine->children[0].children.size() == 4);
    const auto ref = *mine->children[0].children[1].ref;
    CHECK(ref.kind == FilterRef::Kind::user);
    CHECK(ref.label() == "h₁ Мой db3");
    auto a = analyze(ref, 512);
    REQUIRE(a);
    CHECK(a->pr_error);
    CHECK(*a->pr_error < 1e-12);
    set_user_banks({});
    const auto empty = build_library();
    for (const auto& g : empty.children)
        if (g.label == "Мои фильтры") CHECK(!g.later.empty());
}

TEST_CASE("saving the same name twice keeps both banks reachable", "[tui][user_filters]") {
    const auto dir = fresh_dir("signals-user-filters-unique");
    UserBank a{"p3-k2", core::wavelet("db3")->h0, core::wavelet("db3")->f0, ""};
    UserBank b{"p3-k2", core::wavelet("haar")->h0, core::wavelet("haar")->f0, ""};
    REQUIRE(save_user_bank(dir, a));
    REQUIRE(save_user_bank(dir, b));
    CHECK(a.name == "p3-k2");
    CHECK(b.name == "p3-k2-2");
    const auto lib = load_user_banks(dir);
    REQUIRE(lib.banks.size() == 2);
    CHECK(lib.banks[0].name != lib.banks[1].name);
}
