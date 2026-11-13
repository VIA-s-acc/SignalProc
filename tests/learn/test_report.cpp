#include <catch2/catch_test_macros.hpp>

#include "i18n/i18n.hpp"
#include "learn/report.hpp"
#include "support/golden_text.hpp"
#include "support/paths.hpp"

using namespace signals::learn;

TEST_CASE("the report matches the golden file", "[learn][report]") {
    auto files = read_course_dir(signals::test::learn_fixture("mini"));
    REQUIRE(files.has_value());
    auto course = load_course(*files);
    REQUIRE(course.has_value());
    Progress p;
    p.student = "Анна Иванова";
    p.variant = 17;
    p.tasks["1.1"] = {TaskState::solved, 2, 1, "2026-11-13 18:20", "9", {}};
    p.tasks["1.2"] = {TaskState::revealed, 4, 1, "2026-11-13 18:40", "", {"err = 0", "", ""}};
    p.tasks["2.2"] = {TaskState::open, 1, 0, "", "", {}};
    const auto graph = [](std::string_view id) -> std::optional<std::string> {
        if (id == "1.2") return std::string{"pipeline \"double\" v1\n\nnode x = samples(values=\"1 2\", rate=8)\nnode y = add()\n\nx -> y.a\nx -> y.b\n"};
        return std::nullopt;
    };
    signals::LanguageScope ru{signals::Language::ru};
    signals::test::check_golden_text("learn_report_ru", progress_report(*course, p, "0.8.0", "2026-11-13", graph));
    signals::LanguageScope en{signals::Language::en};
    signals::test::check_golden_text("learn_report_en", progress_report(*course, p, "0.8.0", "2026-11-13", graph));
}
