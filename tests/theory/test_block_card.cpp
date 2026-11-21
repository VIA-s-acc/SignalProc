#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "i18n/i18n.hpp"
#include "pipeline/migrate.hpp"
#include "theory/user_card.hpp"

using Catch::Matchers::ContainsSubstring;
using namespace signals;

TEST_CASE("an own block card shows its description, params, typed ports and inner blocks", "[theory][block-card]") {
    const auto loaded = pipeline::load_sig("pipeline \"b\" v2\n# Splits off the approximation\n# and quiets it\nblock analyze(w=db2, n=1) {\n    in x\n    out c, lo\n"
                                           "    node t = dwt(wavelet=$w, levels=$n)\n    node g = gain(db=-6)\n    x -> t -> c\n    t.a -> g -> lo\n}\n");
    REQUIRE(loaded);
    const auto* def = pipeline::find_own_block(loaded->doc, "analyze");
    REQUIRE(def);
    const auto card = theory::user_block_card(*def, loaded->doc);
    CHECK(card.title == "analyze");
    const auto body = [&](const char* key) {
        const auto* s = card.section(tr(key));
        INFO(key);
        REQUIRE(s);
        return s->body;
    };
    CHECK_THAT(body("theory.card.purpose"), ContainsSubstring("Splits off the approximation"));
    CHECK_THAT(body("theory.card.purpose"), ContainsSubstring("and quiets it"));
    CHECK_THAT(body("theory.user_card.params"), ContainsSubstring("`w` = `db2`"));
    CHECK_THAT(body("theory.user_card.params"), ContainsSubstring("`n` = `1`"));
    const auto ports = body("theory.user_card.ports");
    CHECK_THAT(ports, ContainsSubstring("`x`"));
    CHECK_THAT(ports, ContainsSubstring("`c` — coeffs"));
    CHECK_THAT(ports, ContainsSubstring("`lo` — signal"));
    CHECK_THAT(body("theory.user_card.graph"), ContainsSubstring("node t = dwt(wavelet=$w, levels=$n)"));
    const auto links = body("theory.card.links");
    CHECK_THAT(links, ContainsSubstring("`dwt`"));
    CHECK_THAT(links, ContainsSubstring("`gain`"));
}
