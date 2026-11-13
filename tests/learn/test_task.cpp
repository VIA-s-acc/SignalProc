#include <catch2/catch_test_macros.hpp>

#include <string>

#include "i18n/i18n.hpp"
#include "learn/task.hpp"
#include "support/paths.hpp"

using namespace signals::learn;
using signals::test::learn_fixture;

namespace {

FileMap mini() {
    auto files = read_course_dir(learn_fixture("mini"));
    REQUIRE(files.has_value());
    return *files;
}

std::string load_error(const FileMap& files) {
    auto c = load_course(files);
    REQUIRE_FALSE(c.has_value());
    return c.error().message;
}

void replace(FileMap& files, const std::string& path, const std::string& from, const std::string& to) {
    auto& text = files.at(path);
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
}

}  // namespace

TEST_CASE("a course loads chapters and tasks in directory order with ids from prefixes", "[learn][task]") {
    auto c = load_course(mini());
    REQUIRE(c.has_value());
    REQUIRE(c->chapters.size() == 2);
    CHECK(c->chapters[0].id == "1");
    CHECK(c->chapters[1].id == "2");
    CHECK(c->size() == 4);
    {
        signals::LanguageScope en{signals::Language::en};
        CHECK(c->title() == "Mini course");
        CHECK(c->chapters[0].title() == "Basics");
    }
    {
        signals::LanguageScope ru{signals::Language::ru};
        CHECK(c->chapters[0].title() == "Основы");
    }
    const auto* sum = c->find("1.1");
    REQUIRE(sum != nullptr);
    CHECK(sum->kind == TaskKind::answer);
    CHECK(sum->open == "signal");
    CHECK(sum->path == "01-basics/01-sum");
    REQUIRE(sum->answer.has_value());
    CHECK(sum->answer->type == AnswerType::integer);
    CHECK(sum->answer->tol == 0);
    CHECK(sum->answer->expected == "{{a}} + 1");
    REQUIRE(sum->params.size() == 1);
    CHECK(sum->params[0].kind == ParamSpec::Kind::integer);
    CHECK(sum->ru.hints.size() == 2);
    CHECK(sum->en.title == "Sum");

    const auto* dbl = c->find("1.2");
    REQUIRE(dbl != nullptr);
    CHECK(dbl->kind == TaskKind::graph);
    CHECK(dbl->open == "pipeline");
    CHECK(dbl->locked == std::vector<std::string>{"x"});
    REQUIRE(dbl->input.has_value());
    CHECK(dbl->input->node == "x");
    CHECK(dbl->input->hidden == 2);
    REQUIRE(dbl->measures.size() == 2);
    CHECK(dbl->measures[0].kind == "max_error");
    CHECK(dbl->measures[0].ref == "y");
    CHECK(dbl->measures[1].block == "add");
    REQUIRE(dbl->goals.size() == 3);
    CHECK(dbl->goals[0].when == "err <= 1e-12");
    CHECK(dbl->goals[1].require == std::vector<std::pair<std::string, int>>{{"add", 1}});
    CHECK(dbl->goals[2].forbid == std::vector<std::string>{"gain", "affine"});
    CHECK(dbl->start_sig.find("gain(db=0)") != std::string::npos);
    CHECK(dbl->solution_sig.find("add()") != std::string::npos);
    REQUIRE(dbl->cheats.size() == 1);
    CHECK(dbl->cheats[0].first == "hardcoded.sig");
    CHECK(dbl->ru.goals.size() == 3);
    CHECK(c->find("2.1") != nullptr);
    CHECK(c->find("3.1") == nullptr);
}

TEST_CASE("task text splits into statement, goals, hints and solution", "[learn][task]") {
    auto t = parse_task_text("# Title\n\nFirst line\nwith $x$.\n\n## Goals {#goals}\n\n1. One.\n2. Two\n   continued.\n\n"
                             "## Hints {#hints}\n\n1. H1.\n\n## Solution {#solution}\n\nWorked.\n\n### Detail\n\nMore.\n");
    REQUIRE(t.has_value());
    CHECK(t->title == "Title");
    CHECK(t->statement == "First line\nwith $x$.");
    CHECK(t->goals == std::vector<std::string>{"One.", "Two continued."});
    CHECK(t->hints == std::vector<std::string>{"H1."});
    CHECK(t->solution == "Worked.\n\n### Detail\n\nMore.");
    CHECK_FALSE(parse_task_text("no title\n").has_value());
}

TEST_CASE("errors name the file and the line", "[learn][task]") {
    {
        auto f = mini();
        replace(f, "01-basics/01-sum/task.toml", "open = \"signal\"", "open = = 1");
        CHECK(load_error(f).find("01-basics/01-sum/task.toml:2") != std::string::npos);
    }
    {
        auto f = mini();
        replace(f, "01-basics/01-sum/task.toml", "kind = \"answer\"", "kind = \"graf\"");
        const auto e = load_error(f);
        CHECK(e.find("01-basics/01-sum/task.toml") != std::string::npos);
        CHECK(e.find("graf") != std::string::npos);
    }
    {
        auto f = mini();
        f.erase("01-basics/01-sum/en.md");
        CHECK(load_error(f).find("01-basics/01-sum/en.md") != std::string::npos);
    }
    {
        auto f = mini();
        replace(f, "01-basics/02-double/ru.md", "3. Усиление не используется.\n", "");
        CHECK(load_error(f).find("01-basics/02-double/ru.md") != std::string::npos);
    }
    {
        auto f = mini();
        replace(f, "01-basics/02-double/task.toml", "kind = \"count\"", "kind = \"volume\"");
        const auto e = load_error(f);
        CHECK(e.find("volume") != std::string::npos);
        CHECK(e.find("task.toml:") != std::string::npos);
    }
    {
        auto f = mini();
        f.erase("01-basics/02-double/solution.sig");
        CHECK(load_error(f).find("solution.sig") != std::string::npos);
    }
    {
        auto f = mini();
        f["basics/chapter.toml"] = "title = \"x\"\n";
        CHECK(load_error(f).find("basics") != std::string::npos);
    }
}

TEST_CASE("tasks are found by book section", "[learn][task]") {
    auto c = load_course(mini());
    REQUIRE(c.has_value());
    auto ids = [](const std::vector<const Task*>& v) {
        std::vector<std::string> out;
        for (const auto* t : v) out.push_back(t->id);
        return out;
    };
    CHECK(ids(tasks_for_section(*c, "conv-signals")) == std::vector<std::string>{"1.2"});
    CHECK(ids(tasks_for_section(*c, "lti-convolution")) == std::vector<std::string>{"1.1", "1.2", "2.1"});
    CHECK(tasks_for_section(*c, "haar").empty());
}
